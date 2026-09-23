"""Automatic BLE reconnection while preserving one client owner."""

from __future__ import annotations

import asyncio
import logging
from collections.abc import Awaitable, Callable

from quotaframe_bridge.protocol.messages import CommandRejected, IncompatibleProtocolError

from quotaframe_bridge.transports.base import (
    TransportCleanupError,
    TransportConnectionError,
    TransportReset,
    UsageTransport,
    WritePolicy,
)

LOGGER = logging.getLogger(__name__)


class ReconnectBackoff:
    """Deterministic exponential delays with an inclusive cap."""

    def __init__(self, initial: float = 2.0, maximum: float = 60.0) -> None:
        if initial <= 0 or maximum < initial:
            raise ValueError("backoff requires 0 < initial <= maximum")
        self.initial = initial
        self.maximum = maximum
        self._next = initial

    def next_delay(self) -> float:
        delay = self._next
        self._next = min(self.maximum, self._next * 2)
        return delay

    def reset(self) -> None:
        self._next = self.initial


class DeviceManager:
    """Reconnect a replaceable transport while remaining its sole owner."""

    def __init__(
        self,
        transport_factory: Callable[[], UsageTransport],
        *,
        on_status: Callable = lambda status: None,
        backoff: ReconnectBackoff | None = None,
        sleep: Callable[[float], Awaitable[None]] = asyncio.sleep,
    ) -> None:
        self._factory = transport_factory
        self._on_status = on_status
        self._backoff = ReconnectBackoff(maximum=5.0) if backoff is None else backoff
        self._sleep = sleep
        self._transport: UsageTransport | None = None
        self._ever_connected = False
        self._closing = False
        self.incompatible = False
        self.cleanup_error: TransportCleanupError | None = None
        self._retry = asyncio.Event()

    def _require_reusable(self) -> None:
        if self.cleanup_error is not None:
            raise self.cleanup_error

    async def _close_transport(self, transport: UsageTransport) -> None:
        try:
            await transport.close()
        except TransportCleanupError as exc:
            self.cleanup_error = exc
            raise

    @property
    def current_transport(self):
        """Identity of the currently owned connection, for operation preconditions."""
        return self._transport

    @property
    def connected(self) -> bool:
        """Whether a live transport is currently held."""

        return self.cleanup_error is None and self._transport is not None and getattr(self._transport, "connected", True)

    @property
    def ever_connected(self) -> bool:
        """Whether this manager has ever held a live transport.

        The tray uses this to tell "a board the user does not own" apart from
        "a board that dropped", so it must never be reset.
        """

        return self._ever_connected

    @property
    def device_status(self):
        """Return negotiated device status from the live transport, if any."""

        return None if not self.connected else getattr(self._transport, "status", None)

    async def reset(self) -> None:
        """Drop the current transport and resume a paused compatibility check.

        Unlike close(), this leaves the manager reusable: _closing stays
        False, so connect() will run again on the next send_command().
        """

        self._require_reusable()
        self.incompatible = False
        self._retry.set()
        if self._transport is not None:
            await self._close_transport(self._transport)
            self._transport = None

    async def connect(self) -> None:
        """Retry link failures; pause incompatible devices until reset or close."""

        self._require_reusable()
        self._closing = False
        if self.connected:
            return
        if self._transport is not None:
            transport, self._transport = self._transport, None
            await self._close_transport(transport)
        while not self._closing:
            # Preserve reset and close signals received during negotiation.
            self._retry.clear()
            transport = self._factory()
            try:
                await transport.connect()
            except TransportCleanupError as exc:
                self.cleanup_error = exc
                raise
            except asyncio.CancelledError:
                await self._close_transport(transport)
                raise
            except IncompatibleProtocolError as exc:
                self.incompatible = True
                await self._close_transport(transport)
                LOGGER.error("BLE protocol incompatible (%s); automatic retries paused", exc)
                await self._retry.wait()
                continue
            except Exception as exc:
                await self._close_transport(transport)
                delay = self._backoff.next_delay()
                detail = (
                    str(exc)
                    if isinstance(exc, TransportConnectionError)
                    else type(exc).__name__
                )
                LOGGER.warning(
                    "BLE connection unavailable (%s); retrying in %.0fs",
                    detail,
                    delay,
                )
                await self._sleep(delay)
                continue
            self._transport = transport
            self.incompatible = False
            self._ever_connected = True
            self._backoff.reset()
            if self.device_status is not None:
                self._on_status(self.device_status)
            return
        raise asyncio.CancelledError

    async def send_command(
        self,
        payload: bytes,
        command: str,
        expected_ack_n: int,
        write_policy: WritePolicy = WritePolicy.NORMAL,
    ) -> None:
        """Send once; report rejection or link loss without retrying the command."""

        self._require_reusable()
        if self._transport is None:
            await self.connect()
        assert self._transport is not None
        try:
            await self._transport.send_command(
                payload, command, expected_ack_n, write_policy
            )
        except asyncio.CancelledError:
            raise
        except CommandRejected:
            raise
        except Exception as exc:
            transport, self._transport = self._transport, None
            await self._close_transport(transport)
            raise TransportReset("BLE link lost; command outcome is unknown") from exc

    async def query_status(self):
        """Query status on the live transport; failures invalidate that transport."""

        self._require_reusable()
        if self._transport is None:
            await self.connect()
        assert self._transport is not None
        query = getattr(self._transport, "query_status", None)
        if query is None:
            raise TransportConnectionError("transport does not support status queries")
        try:
            status = await query()
            self._on_status(status)
            return status
        except asyncio.CancelledError:
            raise
        except Exception as exc:
            await self._close_transport(self._transport)
            self._transport = None
            raise TransportReset("BLE status query reset") from exc

    async def close(self) -> None:
        """Stop reconnect attempts and release the currently owned transport."""

        self._closing = True
        self._retry.set()
        if self._transport is not None:
            await self._close_transport(self._transport)
            self._transport = None
