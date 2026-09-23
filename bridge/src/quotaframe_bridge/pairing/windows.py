"""Authenticated Windows custom pairing for DisplayOnly BLE peripherals."""

from __future__ import annotations

import asyncio
import concurrent.futures
import logging
import re
import threading
import time
from collections.abc import Awaitable, Callable
from dataclasses import dataclass
from typing import Any

from quotaframe_bridge.pairing.base import PairingBusyError, PairingError

PinProvider = Callable[[str], Awaitable[str]]
BindingsLoader = Callable[[], "WinRtBindings"]
Sleep = Callable[[float], Awaitable[None]]
Clock = Callable[[], float]
logger = logging.getLogger(__name__)

__all__ = [
    "BindingsLoader",
    "PairingBusyError",
    "PairingError",
    "PinProvider",
    "WinRtBindings",
    "WindowsPairer",
    "load_winrt_bindings",
    "validate_pin",
]


@dataclass(frozen=True)
class WinRtBindings:
    """Late-bound WinRT types so dry-run and tests stay platform independent."""

    BluetoothLEDevice: Any
    DeviceInformation: Any
    DevicePairingKinds: Any
    DevicePairingProtectionLevel: Any
    DevicePairingResultStatus: Any
    DeviceUnpairingResultStatus: Any


def load_winrt_bindings() -> WinRtBindings:
    """Load the Windows projections installed alongside Bleak."""

    try:
        from winrt.windows.devices.bluetooth import BluetoothLEDevice
        from winrt.windows.devices.enumeration import (
            DeviceInformation,
            DevicePairingKinds,
            DevicePairingProtectionLevel,
            DevicePairingResultStatus,
            DeviceUnpairingResultStatus,
        )
    except (ImportError, OSError) as exc:
        raise PairingError("Windows pairing support is unavailable") from None

    return WinRtBindings(
        BluetoothLEDevice=BluetoothLEDevice,
        DeviceInformation=DeviceInformation,
        DevicePairingKinds=DevicePairingKinds,
        DevicePairingProtectionLevel=DevicePairingProtectionLevel,
        DevicePairingResultStatus=DevicePairingResultStatus,
        DeviceUnpairingResultStatus=DeviceUnpairingResultStatus,
    )


def validate_pin(candidate: str) -> str:
    """Return an exact six-digit ASCII PIN without normalizing user input."""

    if (
        not isinstance(candidate, str)
        or len(candidate) != 6
        or not candidate.isascii()
        or not candidate.isdecimal()
    ):
        raise PairingError("pairing code must contain exactly six ASCII digits")
    return candidate


def _parse_bluetooth_address(device: Any) -> int:
    value = getattr(device, "address", None)
    if not isinstance(value, str):
        raise PairingError("device does not expose a Bluetooth address")
    compact = value.replace(":", "").replace("-", "")
    if re.fullmatch(r"[0-9A-Fa-f]{12}", compact) is None:
        raise PairingError("device has an invalid Bluetooth address")
    return int(compact, 16)


def _status_label(enum_type: Any, status: Any) -> str:
    """Return only a public enum member name, never platform/user details."""

    name = getattr(status, "name", None)
    if isinstance(name, str) and name:
        return name.lower()
    for candidate in dir(enum_type):
        if not candidate.isupper():
            continue
        try:
            if getattr(enum_type, candidate) == status:
                return candidate.lower()
        except Exception:
            continue
    return "unknown"


class WindowsPairer:
    """Own the WinRT PROVIDE_PIN ceremony before Bleak opens GATT."""

    def __init__(
        self,
        pin_provider: PinProvider,
        *,
        repair: bool = False,
        timeout: float = 60.0,
        ceremony_lock: asyncio.Lock | None = None,
        poll_interval: float = 1.0,
        sleep: Sleep = asyncio.sleep,
        clock: Clock = time.monotonic,
        bindings_loader: BindingsLoader = load_winrt_bindings,
    ) -> None:
        if timeout <= 0:
            raise ValueError("pairing timeout must be positive")
        if poll_interval <= 0:
            raise ValueError("pairing poll interval must be positive")
        self._pin_provider = pin_provider
        self._repair_requested = bool(repair)
        self._timeout = timeout
        self._ceremony_lock = ceremony_lock
        self._poll_interval = poll_interval
        self._sleep = sleep
        self._clock = clock
        self._bindings_loader = bindings_loader

    def request_repair(self) -> None:
        """Ask the next pairing attempt to drop the existing bond first."""

        self._repair_requested = True

    def consume_repair(self) -> bool:
        """Return and clear the one-shot repair flag."""

        requested = self._repair_requested
        self._repair_requested = False
        return requested

    async def ensure_paired(self, device: Any) -> None:
        """Ensure an authenticated Windows bond exists for a scanned device."""

        address = _parse_bluetooth_address(device)
        try:
            bindings = self._bindings_loader()
        except PairingError:
            raise
        except Exception:
            raise PairingError("Windows pairing support is unavailable") from None

        information, device_name = await self._open_device_information(
            bindings,
            address,
        )
        pairing = information.pairing

        if self.consume_repair():
            if pairing.is_paired:
                await self._unpair(bindings, pairing)
            information, device_name = await self._open_device_information(
                bindings,
                address,
            )
            pairing = information.pairing

        if pairing.is_paired:
            return

        deadline = self._clock() + self._timeout
        try:
            if self._ceremony_lock is None:
                await self._pair_if_needed(bindings, information, device_name)
                return

            async with self._ceremony_lock:
                information, device_name = await self._open_device_information(
                    bindings,
                    address,
                )
                await self._pair_if_needed(bindings, information, device_name)
        except PairingBusyError:
            await self._wait_for_existing_pairing(
                bindings,
                address,
                device_name,
                deadline,
            )

    async def _pair_if_needed(
        self,
        bindings: WinRtBindings,
        information: Any,
        device_name: str,
    ) -> None:
        pairing = information.pairing
        if pairing.is_paired:
            return
        if not pairing.can_pair:
            raise PairingError("Windows reports that the BLE device cannot pair")
        await self._pair(bindings, pairing.custom, device_name)

    async def _wait_for_existing_pairing(
        self,
        bindings: WinRtBindings,
        address: int,
        device_name: str,
        deadline: float,
    ) -> None:
        while True:
            information, _ = await self._open_device_information(
                bindings,
                address,
            )
            if information.pairing.is_paired:
                return
            remaining = deadline - self._clock()
            if remaining <= 0:
                raise PairingBusyError(
                    f"Windows pairing for {device_name} is still in progress; "
                    "close Add a device, stop other Bridge instances, then "
                    "retry the scoped pairing repair"
                )
            await self._sleep(min(self._poll_interval, remaining))

    async def _open_device_information(
        self,
        bindings: WinRtBindings,
        address: int,
    ) -> tuple[Any, str]:
        try:
            bluetooth_device = (
                await bindings.BluetoothLEDevice.from_bluetooth_address_async(
                    address
                )
            )
        except Exception:
            raise PairingError("Windows could not open the BLE device") from None
        if bluetooth_device is None:
            raise PairingError("Windows BLE device information is unavailable")

        try:
            original_information = bluetooth_device.device_information
            device_id = original_information.id
            device_name = (
                getattr(bluetooth_device, "name", None)
                or getattr(original_information, "name", None)
                or "usage panel"
            )
        except Exception:
            raise PairingError("Windows BLE device information is unavailable") from None
        finally:
            try:
                bluetooth_device.close()
            except Exception:
                pass

        try:
            information = await bindings.DeviceInformation.create_from_id_async(
                device_id
            )
        except Exception:
            raise PairingError("Windows could not refresh pairing state") from None
        if information is None:
            raise PairingError("Windows pairing state is unavailable")
        return information, str(device_name)

    async def _unpair(self, bindings: WinRtBindings, pairing: Any) -> None:
        try:
            result = await pairing.unpair_async()
        except Exception:
            raise PairingError("Windows unpair failed") from None
        success = (
            bindings.DeviceUnpairingResultStatus.UNPAIRED,
            bindings.DeviceUnpairingResultStatus.ALREADY_UNPAIRED,
        )
        if result.status not in success:
            raise PairingError("Windows unpair failed")

    async def _pair(
        self,
        bindings: WinRtBindings,
        custom_pairing: Any,
        device_name: str,
    ) -> None:
        """Run one authenticated WinRT ceremony and drain all callback work."""

        provide_pin = bindings.DevicePairingKinds.PROVIDE_PIN
        protection = (
            bindings.DevicePairingProtectionLevel.ENCRYPTION_AND_AUTHENTICATION
        )
        loop = asyncio.get_running_loop()
        # WinRT may invoke pairing_requested on a platform callback thread while
        # the PIN provider must run on this asyncio loop. This registry bridges
        # those two domains and lets teardown cancel/drain every scheduled task.
        request_futures: list[concurrent.futures.Future[None]] = []
        request_errors: list[PairingError] = []
        request_registry_lock = threading.Lock()
        request_closing = False

        async def accept_request(
            args: Any,
            complete_deferral: Callable[[], None],
        ) -> None:
            try:
                if args.pairing_kind != provide_pin:
                    raise PairingError("unsupported pairing ceremony")
                try:
                    candidate = await self._pin_provider(device_name)
                except Exception:
                    raise PairingError("pairing code provider failed") from None
                args.accept_with_pin(validate_pin(candidate))
            except PairingError as exc:
                request_errors.append(exc)
            except Exception:
                request_errors.append(PairingError("Windows rejected the pairing code"))
            finally:
                complete_deferral()

        def pairing_requested(_sender: Any, args: Any) -> None:
            try:
                deferral = args.get_deferral()
            except Exception:
                request_errors.append(
                    PairingError("Windows pairing request could not start")
                )
                return

            completion_lock = threading.Lock()
            completion_done = False

            def complete_deferral() -> None:
                nonlocal completion_done
                # Both the coroutine's finally block and the Future callback can
                # arrive here. WinRT requires exactly one completion per deferral.
                with completion_lock:
                    if completion_done:
                        return
                    completion_done = True
                try:
                    deferral.complete()
                except Exception:
                    request_errors.append(
                        PairingError("Windows pairing request could not complete")
                    )

            with request_registry_lock:
                if request_closing:
                    complete_deferral()
                    return
                request = accept_request(args, complete_deferral)
                try:
                    future = asyncio.run_coroutine_threadsafe(request, loop)
                except Exception:
                    request.close()
                    complete_deferral()
                    request_errors.append(
                        PairingError("Windows pairing request could not start")
                    )
                    return
                request_futures.append(future)
            future.add_done_callback(lambda _future: complete_deferral())

        async def close_request_registry() -> tuple[
            concurrent.futures.Future[None], ...
        ]:
            nonlocal request_closing
            # Flip the closing flag under the same lock used by the callback,
            # then snapshot the registry. No callback can append a Future after
            # this snapshot, so teardown has a complete set to cancel and drain.
            while not request_registry_lock.acquire(blocking=False):
                await asyncio.sleep(0)
            try:
                request_closing = True
                return tuple(request_futures)
            finally:
                request_registry_lock.release()

        try:
            token = custom_pairing.add_pairing_requested(pairing_requested)
        except Exception:
            raise PairingError("Windows pairing request could not start") from None

        result: Any | None = None
        operation: Any | None = None
        platform_failed = False
        timed_out = False
        try:
            try:
                async with asyncio.timeout(self._timeout):
                    operation = (
                        custom_pairing.pair_with_protection_level_async(
                            provide_pin, protection
                        )
                    )
                    try:
                        result = await operation
                    except asyncio.CancelledError:
                        cancel = getattr(operation, "cancel", None)
                        if callable(cancel):
                            try:
                                cancel()
                            except Exception:
                                pass
                        raise
            except TimeoutError:
                timed_out = True
            except Exception:
                platform_failed = True
        finally:
            # Unregister first so no new platform callback can start while the
            # existing callback tasks are being cancelled and drained below.
            try:
                custom_pairing.remove_pairing_requested(token)
            except Exception:
                if not request_errors:
                    request_errors.append(
                        PairingError("Windows pairing request cleanup failed")
                    )
            registered_futures = await close_request_registry()
            for future in registered_futures:
                if not future.done():
                    future.cancel()
            if registered_futures:
                await asyncio.gather(
                    *(
                        asyncio.wrap_future(future)
                        for future in registered_futures
                    ),
                    return_exceptions=True,
                )

        if timed_out:
            raise PairingError("Windows pairing timed out")
        if request_errors:
            raise request_errors[0]
        if platform_failed or result is None:
            raise PairingError("Windows pairing failed")
        success = (
            bindings.DevicePairingResultStatus.PAIRED,
            bindings.DevicePairingResultStatus.ALREADY_PAIRED,
        )
        busy = getattr(
            bindings.DevicePairingResultStatus,
            "OPERATION_ALREADY_IN_PROGRESS",
            None,
        )
        if busy is not None and result.status == busy:
            logger.warning(
                "Windows pairing result: %s",
                _status_label(bindings.DevicePairingResultStatus, result.status),
            )
            raise PairingBusyError("Windows pairing operation is already active")
        if result.status not in success:
            logger.warning(
                "Windows pairing result: %s",
                _status_label(bindings.DevicePairingResultStatus, result.status),
            )
            raise PairingError("Windows pairing failed")
