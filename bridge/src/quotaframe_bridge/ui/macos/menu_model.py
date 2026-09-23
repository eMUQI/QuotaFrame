"""The menu bar menu as data, with no AppKit in sight.

Menu labels and action groups are independent of the native UI.
`status_item.py` renders this model as an NSMenu.

Localized copy is maintained in `quotaframe_bridge.catalog`.
"""

from __future__ import annotations

import enum
from dataclasses import dataclass

from quotaframe_bridge.i18n import tr

# Shown until the first snapshot arrives. Empty strings would render as two
# blank rows, which reads as a broken panel rather than one that has not
# published yet.
STARTING_LINES = (tr("starting_devices"), tr("starting_usage"))

REFRESH_LABEL = tr("refresh")
ADD_DEVICE_LABEL = tr("add_device")
# Trailing ellipsis, because on macOS this opens a dialog instead of acting:
# the platform has no unpair API, so the Bridge can only explain the manual
# steps. See `dialogs.repair_guidance`.
REPAIR_LABEL = tr("repair_menu")
# macOS calls this a login item, not a startup entry.
AUTOSTART_LABEL = tr("autostart_macos")
# Trailing ellipsis for the same reason as REPAIR_LABEL: this opens a dialog
# that asks which board to drop, rather than acting on the spot. macOS has no
# per-device submenu, so the choice has to happen in the alert.
FORGET_LABEL = tr("forget_menu")
OPEN_LOG_LABEL = tr("open_log")
QUIT_LABEL = tr("quit")


class MenuAction(enum.Enum):
    """What a selectable row asks the controller to do."""

    TOGGLE_SCREEN = "toggle_screen"
    PREVIOUS_PAGE = "previous_page"
    NEXT_PAGE = "next_page"
    REFRESH = "refresh"
    ADD_DEVICE = "add_device"
    FIRMWARE_UPDATE = "firmware_update"
    BRIDGE_UPDATE = "bridge_update"
    REPAIR = "repair"
    FORGET_DEVICE = "forget_device"
    TOGGLE_AUTOSTART = "toggle_autostart"
    OPEN_LOG = "open_log"
    QUIT = "quit"


@dataclass(frozen=True, slots=True)
class MenuItem:
    """One row. `action` of None marks a disabled information row."""

    label: str
    action: MenuAction | None = None
    checked: bool = False
    separator: bool = False
    action_enabled: bool = True
    device_address: str | None = None

    @property
    def enabled(self) -> bool:
        return self.action is not None and self.action_enabled

    @property
    def is_checkbox(self) -> bool:
        return self.action is MenuAction.TOGGLE_AUTOSTART


SEPARATOR = MenuItem(label="", separator=True)


def build_menu(
    info_lines: tuple[str, ...] = STARTING_LINES,
    *,
    autostart_checked: bool = False,
    bridge_update_enabled: bool = True,
    bridge_update_detail: str = tr("check_bridge_update"),
    forget_enabled: bool = False,
    firmware_actions: tuple[tuple[str, str, bool, str], ...] = (),
) -> tuple[MenuItem, ...]:
    """Build status, screen controls, device actions, and application commands.

    Device removal remains disabled until the Bridge owns a device.
    Firmware actions route by device address; labels are presentation text.
    """

    devices, providers = _two_lines(info_lines)
    return (
        MenuItem(label=devices),
        MenuItem(label=providers),
        SEPARATOR,
        MenuItem(label=tr("toggle_screen"), action=MenuAction.TOGGLE_SCREEN),
        MenuItem(label=tr("previous_page"), action=MenuAction.PREVIOUS_PAGE),
        MenuItem(label=tr("next_page"), action=MenuAction.NEXT_PAGE),
        SEPARATOR,
        MenuItem(label=REFRESH_LABEL, action=MenuAction.REFRESH),
        MenuItem(label=ADD_DEVICE_LABEL, action=MenuAction.ADD_DEVICE),
        MenuItem(
            label=bridge_update_detail,
            action=MenuAction.BRIDGE_UPDATE,
            action_enabled=bridge_update_enabled,
        ),
        *(MenuItem(
            label=f"{label} · {tr('firmware_detail', detail=detail)}",
            action=MenuAction.FIRMWARE_UPDATE,
            action_enabled=enabled,
            device_address=address,
        ) for address, label, enabled, detail in firmware_actions),
        MenuItem(label=REPAIR_LABEL, action=MenuAction.REPAIR),
        MenuItem(
            label=FORGET_LABEL,
            action=MenuAction.FORGET_DEVICE,
            action_enabled=forget_enabled,
        ),
        SEPARATOR,
        MenuItem(
            label=AUTOSTART_LABEL,
            action=MenuAction.TOGGLE_AUTOSTART,
            checked=autostart_checked,
        ),
        MenuItem(label=OPEN_LOG_LABEL, action=MenuAction.OPEN_LOG),
        SEPARATOR,
        MenuItem(label=QUIT_LABEL, action=MenuAction.QUIT),
    )


def _two_lines(info_lines: tuple[str, ...]) -> tuple[str, str]:
    """Coerce whatever the controller supplied into exactly two rows."""

    lines = tuple(line for line in info_lines if line)
    if len(lines) >= 2:
        return lines[0], lines[1]
    if len(lines) == 1:
        return lines[0], STARTING_LINES[1]
    return STARTING_LINES
