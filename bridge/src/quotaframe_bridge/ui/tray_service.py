"""Tray projections of the shared device service."""

from quotaframe_bridge.service.devices import PairerT, TrayServiceGraph
from quotaframe_bridge.ui.status import DeviceStatus, PanelStatus


def snapshot_tray_status(
    graph: TrayServiceGraph[PairerT],
    *,
    adoption_running: bool = False,
) -> PanelStatus:
    """Render the platform-independent service state consumed by tray shells."""

    devices = tuple(
        DeviceStatus(
            device.name,
            connected=device.manager.connected,
            ever_connected=device.manager.ever_connected,
            incompatible=device.manager.incompatible,
            cleanup_failed=device.manager.cleanup_error is not None,
            address=device.address,
            metadata_error=device.metadata_error,
            session=device.session,
        )
        for device in graph.devices
    )
    return PanelStatus(
        devices=devices,
        providers=dict(graph.service.store.providers),
        consecutive_failures=graph.service.consecutive_failures,
        resolution_error=graph.service.resolution_error,
        adoption_running=adoption_running,
        incompatible_found=bool(graph.incompatible_devices),
        devices_unreadable=graph.store.read_error,
    )
