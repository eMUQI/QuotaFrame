"""Capability-gated host-local clock synchronization."""

from __future__ import annotations

from collections.abc import Callable
from datetime import datetime

from quotaframe_bridge.protocol.messages import (
    AckError,
    DeviceStatus,
    Sequence,
    encode_time_sync,
)
from quotaframe_bridge.transports.base import UsageTransport


class TimeSyncScheduler:
    """Rate-limit host-local RTC updates for devices that advertise time.sync.v1."""

    # Re-sync often enough to correct ordinary RTC drift and timezone changes
    # without coupling every usage publication to an RTC write.
    INTERVAL_SECONDS = 6 * 60 * 60

    def __init__(
        self,
        wall_now: Callable[[], datetime],
        monotonic_now: Callable[[], float],
    ) -> None:
        self._wall_now = wall_now
        self._monotonic_now = monotonic_now
        self._next_due: float | None = None

    @staticmethod
    def is_supported(status: DeviceStatus | None) -> bool:
        """Return whether negotiated device capabilities include time.sync.v1."""

        return status is not None and "time.sync.v1" in status.capabilities

    async def sync_if_due(
        self,
        transport: UsageTransport,
        sequence: Sequence,
        *,
        force: bool = False,
    ) -> bool:
        """Send one time sync when due and return whether the device accepted it.

        A protocol-level ACK rejection is treated as a declined optional sync
        and returns False. Transport failures still propagate so the caller can
        reset/reconnect the link and force a later retry.
        """

        now = self._monotonic_now()
        if not force and self._next_due is not None and now < self._next_due:
            return False

        command_sequence = sequence.next()
        payload = encode_time_sync(self._wall_now(), command_sequence)
        try:
            await transport.send_command(
                payload,
                "time_sync",
                command_sequence,
            )
        except AckError:
            return False

        self._next_due = now + self.INTERVAL_SECONDS
        return True
