"""Explicit adoption through secure negotiation and atomic storage."""

from __future__ import annotations

import asyncio
import logging
from collections.abc import Callable
from dataclasses import dataclass

from quotaframe_bridge.pairing.base import DevicePairer, PairingError
from quotaframe_bridge.protocol.messages import IncompatibleProtocolError, ProtocolError
from quotaframe_bridge.transports.bleak_nus import (
    BleakNusTransport,
    NusTransportError,
)

LOGGER = logging.getLogger(__name__)

# How many consecutive empty scans end an adoption run. Three lets a board
# that is still booting be picked up, without leaving the scan running.
EMPTY_SCAN_LIMIT = 3


@dataclass(frozen=True, slots=True)
class Discovery:
    """What one adoption scan learned.

    `declined` carries the address of a board that answered but whose pairing
    failed or was cancelled, so the caller can stop offering it. It is
    separate from `target`/`address` because a decline is not a candidate.
    """

    target: str = ""
    address: str = ""
    declined: str = ""
    name: str = ""
    firmware_project: str = ""

    @property
    def found(self) -> bool:
        return bool(self.name and self.address)


async def discover_device(
    *,
    pairer: DevicePairer,
    exclude_addresses: frozenset[str] = frozenset(),
    scan_timeout: float = 10.0,
    device_name: str | None = None,
    name_prefix: str | None = None,
    on_incompatible: Callable[[str], None] = lambda address: None,
) -> Discovery:
    """Scan once for a board outside `exclude_addresses` and identify it."""

    transport = BleakNusTransport(
        pairer=pairer,
        device_name=device_name,
        name_prefix=name_prefix,
        exclude_addresses=exclude_addresses,
        scan_timeout=scan_timeout,
    )
    try:
        try:
            await transport.connect()
        except IncompatibleProtocolError as exc:
            address = transport.found_address or ""
            LOGGER.warning("incompatible device at %s: %s", address, exc)
            on_incompatible(address)
            return Discovery(declined=address)
        except (NusTransportError, PairingError, ProtocolError) as exc:
            # A cancelled passkey dialog lands here. The address is known only
            # when the scan itself succeeded, which is exactly the case worth
            # remembering: it is the board that must stop re-prompting.
            declined = transport.found_address or ""
            if declined:
                LOGGER.info("adoption declined or failed for %s: %s", declined, exc)
            return Discovery(declined=declined)
        except Exception as exc:
            declined = transport.found_address or ""
            LOGGER.warning(
                "adoption BLE operation failed: %s", type(exc).__name__
            )
            return Discovery(declined=declined)
        status = transport.status
        address = transport.found_address
        if address is None:
            return Discovery()
        if status is None:
            return Discovery(declined=address)
        return Discovery(target=status.target, address=address, name=status.name, firmware_project=status.firmware_project)
    finally:
        # The adoption link is always dropped. An adopted device's session
        # opens its own, and the firmware accepts one peer at a time.
        try:
            await transport.close()
        except asyncio.CancelledError:
            raise
        except Exception as exc:
            LOGGER.warning(
                "adoption link cleanup failed: %s", type(exc).__name__
            )


async def adopt_devices(
    *,
    pairer_factory: Callable[[], DevicePairer],
    excluded: Callable[[], frozenset[str]],
    adopt: Callable[[Discovery], object | None],
    decline: Callable[[str], None] = lambda address: None,
    scan_timeout: float = 10.0,
    retry_delay: float = 20.0,
    empty_scan_limit: int = EMPTY_SCAN_LIMIT,
    on_incompatible: Callable[[str], None] = lambda address: None,
) -> tuple[object, ...]:
    """Adopt every board answering right now, then stop scanning.

    Bounded on purpose. An unbounded scan for boards nobody owns is avoided;
    this stops after a few empty scans rather than running for the life of the
    process. A board plugged in later reaches the same routine through the
    tray's add-device action.
    """

    adopted: list[object] = []
    empty_scans = 0
    while empty_scans < empty_scan_limit:
        discovery = await discover_device(
            pairer=pairer_factory(),
            exclude_addresses=excluded(),
            scan_timeout=scan_timeout,
            on_incompatible=on_incompatible,
        )
        if discovery.declined:
            decline(discovery.declined)
        if not discovery.found:
            empty_scans += 1
            if empty_scans < empty_scan_limit:
                await asyncio.sleep(retry_delay)
            continue
        device = adopt(discovery)
        if device is None:
            decline(discovery.address)
            empty_scans += 1
            if empty_scans < empty_scan_limit:
                await asyncio.sleep(retry_delay)
            continue
        empty_scans = 0
        adopted.append(device)
    return tuple(adopted)
