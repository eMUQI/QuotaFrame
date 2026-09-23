"""Shared owned-device session assembly, metadata updates and explicit adoption."""

from __future__ import annotations

import logging
import asyncio
from collections.abc import Callable
from dataclasses import dataclass, replace
from typing import Generic, TypeVar

from quotaframe_bridge.pairing.base import DevicePairer
from quotaframe_bridge.service.adoption import (
    EMPTY_SCAN_LIMIT,
    Discovery,
    adopt_devices,
)
from quotaframe_bridge.service.device_manager import DeviceManager
from quotaframe_bridge.service.multi_device import (
    DeviceSession,
    MultiDeviceBridgeService,
)
from quotaframe_bridge.service.ownership import (
    OwnedDevice,
    OwnershipStore,
    display_name,
    normalize_address,
)
from quotaframe_bridge.sources.base import UsageSource
from quotaframe_bridge.transports.bleak_nus import BleakNusTransport

LOGGER = logging.getLogger(__name__)

PairerT = TypeVar("PairerT", bound=DevicePairer)


@dataclass(slots=True)
class TrayDevice(Generic[PairerT]):
    """One adopted board and the objects that keep its link alive.

    `name` is recomputed from the owned set. Devices with identical reported
    names receive address suffixes for display disambiguation.
    """

    owned: OwnedDevice
    name: str
    pairer: PairerT
    manager: DeviceManager
    session: DeviceSession
    update_task: asyncio.Task | None = None
    update_cancellable: bool = False
    update_cancel_requested: bool = False
    metadata_error: bool = False

    @property
    def address(self) -> str:
        return self.owned.address


class TrayServiceGraph(Generic[PairerT]):
    """Live device sessions, incrementally attached and removed during ownership changes."""

    def __init__(
        self,
        *,
        service: MultiDeviceBridgeService,
        store: OwnershipStore,
        pairer_factory: Callable[[], PairerT],
        scan_timeout: float = 10.0,
        connect_timeout: float = 60.0,
        ack_timeout: float = 5.0,
    ) -> None:
        self.service = service
        self.store = store
        self._pairer_factory = pairer_factory
        self._scan_timeout = scan_timeout
        self._connect_timeout = connect_timeout
        self._ack_timeout = ack_timeout
        self._devices: dict[str, TrayDevice[PairerT]] = {}
        # Pairing declines are scoped to a single explicit adoption run.
        self._declined: set[str] = set()
        self.incompatible_devices: set[str] = set()

    @property
    def devices(self) -> tuple[TrayDevice[PairerT], ...]:
        return tuple(self._devices.values())

    @property
    def names(self) -> tuple[str, ...]:
        return tuple(device.name for device in self._devices.values())

    def new_pairer(self) -> PairerT:
        """Create a pairer for work that does not belong to a session yet."""

        return self._pairer_factory()

    def get(self, address: str) -> TrayDevice[PairerT] | None:
        normalized = normalize_address(address)
        return None if normalized is None else self._devices.get(normalized)

    def excluded_addresses(self) -> frozenset[str]:
        """Addresses an adoption scan must skip."""

        return frozenset(self._devices) | frozenset(self._declined)

    def decline(self, address: str) -> None:
        """Exclude an address from further pairing attempts in the current adoption run."""

        normalized = normalize_address(address)
        if normalized is not None:
            self._declined.add(normalized)

    def build(self) -> tuple[TrayDevice[PairerT], ...]:
        """Attach sessions for owned devices missing from the live graph."""

        added = []
        for owned in self.store.devices:
            if owned.address in self._devices:
                continue
            added.append(self._attach(owned))
        if added:
            self._rename()
        return tuple(added)

    def adopt(self, discovery: Discovery) -> TrayDevice[PairerT] | None:
        """Persist device ownership before attaching its live session."""

        owned = self.store.add(discovery.target, discovery.address, name=discovery.name,
                               firmware_project=discovery.firmware_project)
        if owned is None:
            return None
        device = self._attach(owned)
        self._rename()
        return device

    async def forget(self, address: str, *, expected) -> bool:
        """Persist ownership removal before cancelling the update and closing the session."""

        device = self.get(address)
        if device is None or device is not expected:
            return False
        if device.session.exclusive_active:
            raise RuntimeError("device is busy with OTA")
        removed = self.store.remove(device.address)
        if not removed:
            return False
        del self._devices[device.address]
        if device.update_task is not None:
            device.update_task.cancel()
            await asyncio.gather(device.update_task, return_exceptions=True)
        await self.service.remove_session(device.session)
        self._rename()
        return True

    def _attach(self, owned: OwnedDevice) -> TrayDevice[PairerT]:
        pairer = self._pairer_factory()
        manager = DeviceManager(
            lambda pairer=pairer, owned=owned: BleakNusTransport(
                pairer=pairer,
                device_name=None,
                address=owned.address,
                scan_timeout=self._scan_timeout,
                connect_timeout=self._connect_timeout,
                ack_timeout=self._ack_timeout,
            ),
            on_status=lambda status: self._metadata(owned.address, manager, status),
        )
        session = DeviceSession(display_name(owned, self.store.devices), manager)
        device = TrayDevice(
            owned=owned,
            name=session.label,
            pairer=pairer,
            manager=manager,
            session=session,
        )
        self._devices[owned.address] = device
        self.service.add_session(session)
        return device

    def _metadata(self, address, manager, status) -> None:
        device = self.get(address)
        if device is None or device.manager is not manager:
            return
        try:
            updated = self.store.update_metadata(address, status)
        except (OSError, ValueError, RuntimeError):
            device.metadata_error = True
            device.owned = replace(device.owned, name=status.name, target=status.target,
                                   firmware_project=status.firmware_project)
            self._rename()
            LOGGER.exception("device metadata could not be saved: %s", address)
            return
        device.metadata_error = False
        if updated is not None:
            device.owned = updated
            self._rename()

    def _rename(self) -> None:
        """Refresh display names after the owned set changed.

        Menu labels and session logs use the same disambiguated device name.
        """

        owned = tuple(device.owned for device in self._devices.values())
        for device in self._devices.values():
            device.name = display_name(device.owned, owned)
            device.session.label = device.name


def build_tray_graph(
    *,
    usage_source_factory: Callable[[], UsageSource],
    pairer_factory: Callable[[], PairerT],
    store: OwnershipStore | None = None,
) -> TrayServiceGraph[PairerT]:
    """Build the shared BLE session graph from platform-specific factories."""

    ownership = OwnershipStore() if store is None else store
    graph = TrayServiceGraph(
        service=MultiDeviceBridgeService(usage_source_factory(), ()),
        store=ownership,
        pairer_factory=pairer_factory,
    )
    graph.build()
    LOGGER.info("tray service assembled for %d owned device(s)", len(graph.devices))
    return graph


async def run_adoption(
    graph: TrayServiceGraph[PairerT],
    *,
    on_adopted: Callable[[TrayDevice[PairerT]], None] = lambda device: None,
    scan_timeout: float = 10.0,
    retry_delay: float = 20.0,
    empty_scan_limit: int = EMPTY_SCAN_LIMIT,
) -> tuple[TrayDevice[PairerT], ...]:
    """Adopt discoverable devices until the consecutive empty-scan limit is reached."""

    def adopt(discovery: Discovery) -> TrayDevice[PairerT] | None:
        device = graph.adopt(discovery)
        if device is not None:
            on_adopted(device)
        return device

    if graph.store.read_error:
        return ()
    graph._declined.clear()
    adopted = await adopt_devices(
        pairer_factory=graph.new_pairer,
        excluded=graph.excluded_addresses,
        adopt=adopt,
        decline=graph.decline,
        scan_timeout=scan_timeout,
        retry_delay=retry_delay,
        empty_scan_limit=empty_scan_limit,
        on_incompatible=graph.incompatible_devices.add,
    )
    LOGGER.info(
        "adoption scan finished: adopted=%d owned=%d",
        len(adopted),
        len(graph.devices),
    )
    return tuple(adopted)  # type: ignore[arg-type]
