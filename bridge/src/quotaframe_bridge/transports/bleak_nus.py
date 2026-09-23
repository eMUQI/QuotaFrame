"""Bleak implementation of the encrypted Nordic UART Service transport."""

from __future__ import annotations

import asyncio
import logging
import sys
from collections.abc import Awaitable, Callable
from typing import Any

from quotaframe_bridge.pairing.base import (
    DevicePairer,
    PairingBusyError,
    PairingError,
    PairingGuidanceError,
)
from quotaframe_bridge.protocol.lines import LineDecodeError, LineDecoder
from quotaframe_bridge.protocol.messages import (
    CommandRejected,
    DeviceStatus,
    ProtocolError,
    encode_status_query,
    parse_ack,
    is_folder_rejection,
    parse_status,
)
from quotaframe_bridge.service.ownership import normalize_address
from quotaframe_bridge.transports.base import TransportCleanupError, TransportConnectionError, WritePolicy

LOGGER = logging.getLogger(__name__)
_STOP_NOTIFY_TIMEOUT = 1.0
_DISCONNECT_TIMEOUT = 3.0
# Failed cleanup must also block replacement managers for the same device.
_QUARANTINED_ADDRESSES: dict[str, TransportCleanupError] = {}

NUS_SERVICE_UUID = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
NUS_RX_UUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
NUS_TX_UUID = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"
PAIRING_FAILURE_MESSAGE = (
    "secure Windows pairing failed"
    if sys.platform == "win32"
    else "secure pairing failed"
)
CONSERVATIVE_CHUNK_CAP = 180
FOLDER_PUSH_CHUNK_CAP = 512
DEFAULT_NAME_PREFIXES = ("QF-",)

DeviceFinder = Callable[..., Awaitable[Any | None]]
ClientFactory = Callable[[Any, Callable[..., None], float], Any]


class NusTransportError(TransportConnectionError):
    """Scanning, pairing, GATT negotiation, writing, or ACK failed."""


def matches_device_name(
    name: object,
    *,
    exact_name: str | None,
    name_prefixes: tuple[str, ...],
) -> bool:
    """Apply user filters within the public QF discovery namespace."""

    if not isinstance(name, str):
        return False
    return (name.startswith("QF-") and len(name) > 3
            and (exact_name is None or name == exact_name)
            and any(name.startswith(prefix) for prefix in name_prefixes))


def chunk_payload(
    payload: bytes,
    reported_size: int | None,
    mtu: int | None,
    write_policy: WritePolicy = WritePolicy.NORMAL,
) -> tuple[bytes, ...]:
    """Split a line using the strictest usable GATT/ATT limit."""

    limits = [
        FOLDER_PUSH_CHUNK_CAP
        if write_policy is WritePolicy.FOLDER_PUSH
        else CONSERVATIVE_CHUNK_CAP
    ]
    if reported_size is not None and reported_size > 0:
        limits.append(reported_size)
    if mtu is not None and mtu > 3:
        limits.append(mtu - 3)
    size = min(limits)
    return tuple(payload[index : index + size] for index in range(0, len(payload), size))


async def _default_device_finder(
    *,
    exact_name: str | None,
    name_prefixes: tuple[str, ...],
    service_uuid: str,
    timeout: float,
    address: str | None = None,
    exclude_addresses: frozenset[str] = frozenset(),
) -> Any | None:
    try:
        from bleak import BleakScanner
    except ImportError as exc:
        raise NusTransportError(
            "Bleak is not installed; install the Bridge package first"
        ) from exc

    service = service_uuid.lower()
    # Normalize here as well as at the caller: this is the boundary where an
    # address becomes a comparison, and a caller passing raw platform text
    # should not silently match nothing.
    wanted = normalize_address(address)

    def matches_address(device: Any) -> bool:
        # An adopted device is matched on address alone. The advertised
        # service UUID would be true as well, but AND-ing it in could only
        # turn a truncated or missed advertisement into a false negative for
        # an already-adopted board.
        candidate = normalize_address(getattr(device, "address", None))
        return candidate is not None and candidate == wanted

    observed: dict[str, tuple[object, set[str]]] = {}

    def matches_advertisement(device: Any, advertisement: Any) -> bool:
        if wanted is not None:
            return matches_address(device)
        # Adoption scans skip boards this user already owns and boards whose
        # pairing was just declined. Without this the scanner keeps returning
        # the same first match, so a neighbour's panel would re-prompt for a
        # passkey the user does not have, once per attempt.
        candidate = normalize_address(getattr(device, "address", None))
        if candidate is not None and candidate in exclude_addresses:
            return False
        name = getattr(advertisement, "local_name", None) or getattr(
            device, "name", None
        )
        if candidate is None:
            return False
        previous_name, advertised = observed.get(candidate, (None, set()))
        name = name or previous_name
        advertised = advertised | {
            uuid.lower() for uuid in (getattr(advertisement, "service_uuids", None) or [])
        }
        observed[candidate] = (name, advertised)
        return service in advertised and matches_device_name(
            name, exact_name=exact_name, name_prefixes=name_prefixes)

    return await BleakScanner.find_device_by_filter(
        matches_advertisement, timeout=timeout
    )


def _default_client_factory(
    device: Any,
    disconnected_callback: Callable[..., None],
    timeout: float,
) -> Any:
    try:
        from bleak import BleakClient
    except ImportError as exc:
        raise NusTransportError(
            "Bleak is not installed; install the Bridge package first"
        ) from exc
    backend_options = (
        {"winrt": {"use_cached_services": False}} if sys.platform == "win32" else {}
    )
    return BleakClient(
        device,
        disconnected_callback=disconnected_callback,
        pair=False,
        timeout=timeout,
        **backend_options,
    )


class BleakNusTransport:
    """One paired NUS connection with serialized commands and strict ACKs."""

    def __init__(
        self,
        *,
        pairer: DevicePairer,
        device_name: str | None = None,
        name_prefix: str | None = None,
        address: str | None = None,
        exclude_addresses: frozenset[str] = frozenset(),
        scan_timeout: float = 10.0,
        connect_timeout: float = 60.0,
        ack_timeout: float = 5.0,
        device_finder: DeviceFinder = _default_device_finder,
        client_factory: ClientFactory = _default_client_factory,
    ) -> None:
        if scan_timeout <= 0 or connect_timeout <= 0 or ack_timeout <= 0:
            raise ValueError("BLE timeouts must be positive")
        self._pairer = pairer
        self.device_name = device_name
        self.name_prefix = name_prefix
        self.address = normalize_address(address)
        self.exclude_addresses = frozenset(
            normalized
            for normalized in (
                normalize_address(value) for value in exclude_addresses
            )
            if normalized is not None
        )
        self.name_prefixes = (
            (name_prefix,) if name_prefix is not None else DEFAULT_NAME_PREFIXES
        )
        self.scan_timeout = scan_timeout
        self.connect_timeout = connect_timeout
        self.ack_timeout = ack_timeout
        self._device_finder = device_finder
        self._client_factory = client_factory
        self._client: Any | None = None
        self._rx: Any | None = None
        self._tx: Any | None = None
        self._decoder = LineDecoder(max_line_bytes=4096)
        self._status_future: asyncio.Future[dict[str, Any]] | None = None
        self._ack_future: asyncio.Future[dict[str, Any]] | None = None
        self._expected_ack: tuple[str, int] | None = None
        # Set on every successful scan so an adoption flow can learn the
        # address of a device it found by advertising name.
        self.found_address: str | None = None
        self._send_lock = asyncio.Lock()
        self._cleanup_task: asyncio.Task[None] | None = None
        self._cleanup_error: TransportCleanupError | None = None
        self._cleanup_operations: set[asyncio.Task] = set()
        self.status: DeviceStatus | None = None

    async def connect(self) -> None:
        if self.address in _QUARANTINED_ADDRESSES:
            raise _QUARANTINED_ADDRESSES[self.address]
        if self._cleanup_error is not None:
            raise self._cleanup_error
        if self._cleanup_task is not None:
            await self.close()
        if self._client is not None:
            await self.close()
        device = await self._device_finder(
            exact_name=self.device_name,
            name_prefixes=self.name_prefixes,
            service_uuid=NUS_SERVICE_UUID,
            timeout=self.scan_timeout,
            address=self.address,
            exclude_addresses=self.exclude_addresses,
        )
        if device is None:
            raise NusTransportError(
                "usage panel was not found: "
                f"{self.device_name or 'unknown'}"
                f" ({self.address})"
            )
        self.found_address = normalize_address(getattr(device, "address", None))
        if self.found_address in _QUARANTINED_ADDRESSES:
            raise _QUARANTINED_ADDRESSES[self.found_address]

        try:
            await self._pairer.ensure_paired(device)
        except PairingBusyError as exc:
            raise NusTransportError(str(exc)) from None
        except PairingGuidanceError as exc:
            raise NusTransportError(str(exc)) from None
        except PairingError:
            raise NusTransportError(PAIRING_FAILURE_MESSAGE) from None

        client = self._client_factory(
            device, self._on_disconnected, self.connect_timeout
        )
        self._client = client
        try:
            try:
                await client.connect()
            except Exception:
                raise NusTransportError(
                    "BLE connection failed; if Windows shows this device as "
                    "paired, retry once with --repair-pairing"
                ) from None
            services = client.services
            self._rx = services.get_characteristic(NUS_RX_UUID)
            self._tx = services.get_characteristic(NUS_TX_UUID)
            if self._rx is None or self._tx is None:
                raise NusTransportError("device does not expose Nordic UART Service")
            await client.start_notify(self._tx, lambda sender, data: self._on_notification(sender, data)
                                      if self._client is client else None)
            self.status = await self.query_status()
            LOGGER.info(
                "BLE connected: device=%s target=%s secure=%s protocol=%d capabilities=%s",
                self.status.name,
                self.status.target or "unknown",
                str(self.status.secure).lower(),
                self.status.protocol,
                ",".join(sorted(self.status.capabilities)),
            )
        except Exception:
            await self.close()
            raise

    async def query_status(self) -> DeviceStatus:
        async with self._send_lock:
            if (
                self._client is None
                or not self._client.is_connected
                or self._rx is None
            ):
                raise NusTransportError("BLE transport is not connected")
            loop = asyncio.get_running_loop()
            self._status_future = loop.create_future()
            try:
                await self._write_line(encode_status_query())
                try:
                    response = await asyncio.wait_for(
                        asyncio.shield(self._status_future),
                        timeout=self.ack_timeout,
                    )
                except TimeoutError as exc:
                    raise NusTransportError("device status timeout") from exc
                self.status = parse_status(response)
                return self.status
            except (Exception, asyncio.CancelledError):
                await self.close()
                raise
            finally:
                self._retire_future(self._status_future)
                self._status_future = None

    async def send_command(
        self,
        payload: bytes,
        command: str,
        expected_ack_n: int,
        write_policy: WritePolicy = WritePolicy.NORMAL,
    ) -> None:
        async with self._send_lock:
            if (
                self._client is None
                or not self._client.is_connected
                or self._rx is None
            ):
                raise NusTransportError("BLE transport is not connected")
            loop = asyncio.get_running_loop()
            self._ack_future = loop.create_future()
            self._expected_ack = (command, expected_ack_n)
            try:
                await self._write_line(payload, write_policy)
                try:
                    response = await asyncio.wait_for(
                        asyncio.shield(self._ack_future),
                        timeout=self.ack_timeout,
                    )
                except TimeoutError as exc:
                    raise NusTransportError("usage ACK timeout") from exc
                parse_ack(response, command, expected_ack_n)
            except CommandRejected:
                raise
            except asyncio.CancelledError:
                await self.close()
                raise
            except (ProtocolError, OSError, RuntimeError) as exc:
                await self.close()
                if isinstance(exc, NusTransportError):
                    raise
                raise NusTransportError("BLE command failed") from exc
            finally:
                self._retire_future(self._ack_future)
                self._ack_future = None
                self._expected_ack = None

    async def _write_line(
        self,
        payload: bytes,
        write_policy: WritePolicy = WritePolicy.NORMAL,
    ) -> None:
        if self._client is None or self._rx is None:
            raise NusTransportError("BLE transport is not connected")
        reported_size = getattr(
            self._rx, "max_write_without_response_size", None
        )
        mtu = getattr(self._client, "mtu_size", None)
        for chunk in chunk_payload(payload, reported_size, mtu, write_policy):
            await self._client.write_gatt_char(
                self._rx,
                chunk,
                response=write_policy is WritePolicy.NORMAL,
            )

    def _on_notification(self, _sender: Any, data: bytearray | bytes) -> None:
        try:
            messages = self._decoder.feed(bytes(data))
        except LineDecodeError as exc:
            self._fail_pending(NusTransportError("invalid device notification"))
            return
        for message in messages:
            if message.get("ack") == "status":
                future = self._status_future
            else:
                expected = self._expected_ack
                response_n = message.get("n")
                if (
                    expected is None
                    or message.get("ack") != expected[0]
                    or isinstance(response_n, bool)
                    or (response_n != expected[1] and not is_folder_rejection(message, expected[0]))
                ):
                    continue
                future = self._ack_future
            if future is not None and not future.done():
                future.set_result(message)

    @property
    def connected(self) -> bool:
        return self._client is not None and self._client.is_connected and self.status is not None

    def _on_disconnected(self, _client: Any) -> None:
        if _client is not self._client:
            return
        self.status = None
        self._fail_pending(NusTransportError("BLE device disconnected"))

    def _fail_pending(self, error: Exception) -> None:
        for future in (self._status_future, self._ack_future):
            if future is not None and not future.done():
                future.set_exception(error)

    @staticmethod
    def _retire_future(
        future: asyncio.Future[dict[str, Any]] | None,
    ) -> None:
        if future is None:
            return
        if not future.done():
            future.cancel()
        elif not future.cancelled():
            future.exception()

    async def close(self) -> None:
        if self._cleanup_error is not None:
            raise self._cleanup_error
        if self._cleanup_task is None:
            client, tx = self._client, self._tx
            self._client = self._rx = self._tx = None
            self.status = None
            self._fail_pending(NusTransportError("BLE transport closed"))
            if client is None:
                return
            self._cleanup_task = asyncio.create_task(self._disconnect(client, tx))
        cleanup = self._cleanup_task
        cancelled = False
        while not cleanup.done():
            try:
                await asyncio.shield(cleanup)
            except asyncio.CancelledError:
                cancelled = True
            except TransportCleanupError:
                break
        try:
            cleanup.result()
        except TransportCleanupError as exc:
            self._cleanup_error = exc
            address = self.found_address or self.address
            if address is not None:
                _QUARANTINED_ADDRESSES[address] = exc
            raise
        finally:
            self._cleanup_task = None
        if cancelled:
            raise asyncio.CancelledError

    async def _cleanup_operation(self, operation: Awaitable, timeout: float) -> bool:
        """Bound waiting independently of the backend's cancellation response."""
        task = asyncio.create_task(operation)
        self._cleanup_operations.add(task)

        def completed(task: asyncio.Task) -> None:
            self._cleanup_operations.discard(task)
            if not task.cancelled():
                task.exception()

        task.add_done_callback(completed)
        done, _ = await asyncio.wait({task}, timeout=timeout)
        if not done:
            task.cancel()
            return False
        if task.cancelled():
            return False
        return task.exception() is None

    async def _disconnect(self, client: Any, tx: Any) -> None:
        """Attempt both cleanup steps within independent finite budgets."""
        stopped = True
        if tx is not None and client.is_connected:
            stopped = await self._cleanup_operation(
                client.stop_notify(tx), _STOP_NOTIFY_TIMEOUT,
            )
        disconnected = await self._cleanup_operation(
            client.disconnect(), _DISCONNECT_TIMEOUT,
        )
        if not stopped or not disconnected:
            raise TransportCleanupError(
                "BLE cleanup failed; automatic recovery stopped. Restart Bridge."
            )
