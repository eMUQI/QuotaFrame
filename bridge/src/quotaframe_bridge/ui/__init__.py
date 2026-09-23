"""Desktop tray shells and their shared service assembly."""

from quotaframe_bridge.service.devices import (
    TrayDevice,
    TrayServiceGraph,
    build_tray_graph,
    run_adoption,
)
from quotaframe_bridge.ui.tray_service import snapshot_tray_status

__all__ = [
    "TrayDevice",
    "TrayServiceGraph",
    "build_tray_graph",
    "run_adoption",
    "snapshot_tray_status",
]
