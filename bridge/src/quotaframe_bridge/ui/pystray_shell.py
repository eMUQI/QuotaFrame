"""The only module that imports pystray; policy decisions belong elsewhere."""

from __future__ import annotations

import ctypes
import itertools
import logging
import queue
import winreg
from collections.abc import Callable

import pystray
from PIL import Image

from quotaframe_bridge.i18n import tr
from quotaframe_bridge.paths import tray_assets
from quotaframe_bridge.ui.status import DeviceStatus, TrayState

LOGGER = logging.getLogger(__name__)

WM_QUERYENDSESSION = 0x0011
WM_ENDSESSION = 0x0016
WM_USER = 0x0400
WM_LBUTTONUP = 0x0202
WM_RBUTTONUP = 0x0205
NIN_BALLOONUSERCLICK = WM_USER + 5
# pystray already claims WM_USER + 10 and + 11 on this window.
WM_PUBLISH = WM_USER + 32
WM_PAGE = WM_USER + 33

THEME_KEY = r"Software\Microsoft\Windows\CurrentVersion\Themes\Personalize"
THEME_VALUE = "SystemUsesLightTheme"
ICON_SIZE = 32

# Localized copy is maintained in quotaframe_bridge.catalog.
STARTING_LINES = (tr("starting_devices"), tr("starting_usage"))
TOOLTIP = "QuotaFrame"
_ICON_INSTANCE_IDS = itertools.count()


def taskbar_is_light() -> bool:
    """Report whether the taskbar is light, defaulting to dark."""

    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, THEME_KEY) as key:
            value, _kind = winreg.QueryValueEx(key, THEME_VALUE)
    except OSError:
        return False
    return bool(value)


def load_icons(*, light: bool | None = None) -> dict[TrayState, Image.Image]:
    """Load one icon per state for the current taskbar theme."""

    theme = "light" if (taskbar_is_light() if light is None else light) else "dark"
    directory = tray_assets()
    return {
        state: Image.open(
            directory / f"tray-{state.value}-{theme}-{ICON_SIZE}.png"
        )
        for state in TrayState
    }


class _MarshalledIcon(pystray.Icon):
    """Keep every Win32 call on the thread that owns the tray window.

    pystray caches its native menu in a handle that the click handler reads
    and hands straight to Windows. Rebuilding that handle from another thread
    can free it out from under a menu that is about to be displayed, and the
    same applies to the icon handle. Both are done here instead, either from
    the click itself or from a message posted to this window.
    """

    def __init__(self, *args: object, **kwargs: object) -> None:
        super().__init__(*args, **kwargs)
        # pystray creates this in its first menu build, which leaves a window
        # in which a click would find the attribute missing.
        self._menu_handle: object = None
        self._publish: Callable[[], None] = lambda: None
        self._on_left_click: Callable[[], None] = lambda: None
        self._on_turn_page: Callable[[int], None] = lambda _direction: None
        self._can_auto_quit: Callable[[], bool] = lambda: True
        self._on_auto_quit: Callable[[], None] = lambda: None
        self._message_handlers[WM_QUERYENDSESSION] = lambda _w, _l: int(self._can_auto_quit())
        self._message_handlers[WM_ENDSESSION] = self._on_end_session
        self._message_handlers[WM_PAGE] = self._on_page
        self._message_handlers[WM_PUBLISH] = self._on_publish

    def _run(self) -> None:
        from quotaframe_bridge.ui.windows_wheel import TrayWheelHook

        # pystray initializes NOTIFYICONDATAW.uID to zero; hID is not a struct field.
        wheel = TrayWheelHook(lambda: self._hwnd, 0, WM_PAGE)
        wheel.start()
        try:
            super()._run()
        finally:
            wheel.stop()

    def _on_end_session(self, wparam: int, _lparam: object) -> int:
        # A false wParam cancels shutdown; querying alone must not stop the app.
        if wparam:
            self._on_auto_quit()
        return 0

    def _on_page(self, wparam: int, _lparam: object) -> int:
        self._on_turn_page(1 if wparam else -1)
        return 0

    def request_publish(self) -> None:
        """Ask the icon thread to apply the latest state. Safe from anywhere.

        Silently does nothing before the window exists; whatever was stored is
        published by the setup callback once the message loop is up.
        """

        window = self._hwnd
        if window is not None:
            ctypes.windll.user32.PostMessageW(window, WM_PUBLISH, 0, 0)

    def _on_publish(self, _wparam: object, _lparam: object) -> int:
        self._publish()
        return 0

    def _on_notify(self, wparam: object, lparam: object) -> object:
        # Balloon callbacks identify the tray icon, not an individual notification.
        # Open the current menu so retained notifications cannot trigger stale actions.
        if lparam == NIN_BALLOONUSERCLICK:
            lparam = WM_RBUTTONUP
        if lparam == WM_LBUTTONUP:
            self._on_left_click()
            return 0
        if lparam == WM_RBUTTONUP:
            self._update_menu()
        return super()._on_notify(wparam, lparam)


class PystrayShell:
    """Drive a pystray icon. Contains no thresholds and no copy decisions."""

    def __init__(
        self,
        *,
        on_refresh: Callable[[], None],
        on_add_device: Callable[[], None],
        on_repair: Callable[[str], None],
        on_firmware_update: Callable[[str], None],
        on_forget_device: Callable[[str], None],
        on_toggle_autostart: Callable[[bool], None],
        on_open_log: Callable[[], None],
        on_quit: Callable[[], None],
        device_labels: tuple[str, ...],
        can_auto_quit: Callable[[], bool] = lambda: True,
        on_auto_quit: Callable[[], None] | None = None,
        on_bridge_update: Callable[[], None] = lambda: None,
        on_toggle_screensaver: Callable[[], None] = lambda: None,
        on_turn_page: Callable[[int], None] = lambda _direction: None,
    ) -> None:
        self._icons = load_icons()
        # Empty strings render as two blank menu rows, which reads as a broken
        # panel rather than one that has not published its first snapshot yet.
        self._info_lines: tuple[str, ...] = STARTING_LINES
        self._autostart_checked = False
        self._state = TrayState.IDLE
        self._tooltip = TOOLTIP
        self._balloons: queue.SimpleQueue[tuple[str, str]] = queue.SimpleQueue()
        self._on_refresh = on_refresh
        self._on_add_device = on_add_device
        self._on_bridge_update = on_bridge_update
        self._on_repair = on_repair
        self._on_firmware_update = on_firmware_update
        self._on_forget_device = on_forget_device
        self._on_toggle_autostart = on_toggle_autostart
        self._on_open_log = on_open_log
        self._on_quit = on_quit
        device_statuses = {
            label: DeviceStatus(label, connected=False, ever_connected=False)
            for label in device_labels
        }
        firmware_actions = {
            label: (False, tr("unavailable")) for label in device_labels
        }
        self._device_menu_state = (device_statuses, firmware_actions)
        # Set when the owned set changes, cleared once the icon thread has
        # rebuilt the native menu. The item *set* is fixed at build time --
        # only text and enabled state are callables -- so adopting or
        # forgetting a device needs a real rebuild, not just new values.
        self._menu_stale = False
        self._bridge_update_action = (True, tr("check_bridge_update"))
        self._icon = _MarshalledIcon(
            f"quotaframe-bridge-{next(_ICON_INSTANCE_IDS)}",
            self._icons[TrayState.IDLE],
            TOOLTIP,
            menu=self._build_menu(),
        )
        self._icon._can_auto_quit = can_auto_quit
        self._icon._on_auto_quit = on_quit if on_auto_quit is None else on_auto_quit
        self._icon._publish = self._publish
        self._icon._on_left_click = on_toggle_screensaver
        self._icon._on_turn_page = on_turn_page

    def _build_menu(self) -> pystray.Menu:
        # Native menu objects can outlive a device-shape update until the icon
        # thread rebuilds them. Each generation retains its mappings as safe
        # fallbacks, while current mappings are switched as one tuple.
        device_statuses, firmware_actions = self._device_menu_state

        def is_current(label):
            current, _ = self._device_menu_state
            return (label in current and current[label].session is device_statuses[label].session
                    and current[label].address == device_statuses[label].address)

        def forget_action(label: str) -> Callable[[object, object], None]:
            address = device_statuses[label].address or label
            def action(_icon: object, _item: object) -> None:
                LOGGER.info("forget device selected: device=%s", label)
                if is_current(label):
                    self._on_forget_device(address)

            return action

        def repair_action(label: str) -> Callable[[object, object], None]:
            address = device_statuses[label].address or label
            def action(_icon: object, _item: object) -> None:
                LOGGER.info("repair menu selected: device=%s", label)
                if is_current(label):
                    self._on_repair(address)

            return action

        def firmware_action(label: str) -> Callable[[object, object], None]:
            address = device_statuses[label].address or label
            def action(_icon: object, _item: object) -> None:
                LOGGER.info("firmware update selected: device=%s", label)
                if is_current(label):
                    self._on_firmware_update(address)

            return action

        def device_status(label: str) -> DeviceStatus:
            current_statuses, _firmware = self._device_menu_state
            return current_statuses.get(label, device_statuses[label])

        def firmware_status(label: str) -> tuple[bool, str]:
            _statuses, current_firmware = self._device_menu_state
            return current_firmware.get(
                label, firmware_actions.get(label, (False, tr("unavailable")))
            )

        def device_text(label: str) -> Callable[[object], str]:
            return lambda _item: (
                f"{device_status(label).label} · {device_status(label).connection_text}"
            )

        def firmware_text(label: str) -> Callable[[object], str]:
            return lambda _item: tr("firmware_detail", detail=firmware_status(label)[1])

        def firmware_enabled(label: str) -> Callable[[object], bool]:
            return lambda _item: firmware_status(label)[0]

        def device_menu(label: str) -> pystray.Menu:
            return pystray.Menu(
                item(
                    firmware_text(label),
                    firmware_action(label),
                    enabled=firmware_enabled(label),
                ),
                item(tr("repair_menu"), repair_action(label)),
                item(tr("forget_menu"), forget_action(label)),
            )

        item = pystray.MenuItem
        return pystray.Menu(
            item(lambda _icon: self._info_lines[0], None, enabled=False),
            item(lambda _icon: self._info_lines[1], None, enabled=False),
            pystray.Menu.SEPARATOR,
            item(tr("refresh"), lambda _icon, _item: self._on_refresh()),
            item(tr("add_device"), lambda _icon, _item: self._on_add_device()),
            pystray.Menu.SEPARATOR,
            *(
                item(device_text(label), device_menu(label))
                for label in device_statuses
            ),
            item(
                lambda _item: self._bridge_update_action[1],
                lambda _icon, _item: self._on_bridge_update(),
                enabled=lambda _item: self._bridge_update_action[0],
            ),
            pystray.Menu.SEPARATOR,
            item(
                tr("autostart_windows"),
                lambda _icon, _item: self._on_toggle_autostart(
                    not self._autostart_checked
                ),
                checked=lambda _item: self._autostart_checked,
            ),
            item(tr("open_log"), lambda _icon, _item: self._on_open_log()),
            pystray.Menu.SEPARATOR,
            item(tr("quit"), lambda _icon, _item: self._on_quit()),
        )

    def run(self) -> None:
        """Attach pystray to Tk's main loop instead of owning the main thread."""

        def setup(icon: pystray.Icon) -> None:
            icon.visible = True
            self._icon.request_publish()

        self._icon.run_detached(setup)

    def _publish(self) -> None:
        """Apply the stored state. Runs on the icon thread and nowhere else."""

        if self._menu_stale:
            self._menu_stale = False
            self._icon.menu = self._build_menu()
        self._icon.icon = self._icons[self._state]
        self._icon.title = self._tooltip
        while True:
            try:
                title, body = self._balloons.get_nowait()
            except queue.Empty:
                return
            self._icon.notify(body, title)

    # Everything below is called from Tk's thread. Each one stores a value and
    # asks the icon thread to do the Win32 work, so no handle is ever freed
    # while the thread that owns the tray window is using it.

    def set_state(self, state: TrayState) -> None:
        self._state = state
        self._icon.request_publish()

    def set_tooltip(self, text: str) -> None:
        self._tooltip = text
        self._icon.request_publish()

    def notify(self, title: str, body: str) -> None:
        self._balloons.put((title, body))
        self._icon.request_publish()

    def set_autostart_checked(self, checked: bool) -> None:
        # Read back when the click handler rebuilds the menu, so no rebuild
        # has to be forced from here.
        self._autostart_checked = checked

    def set_info_lines(self, lines: tuple[str, ...]) -> None:
        self._info_lines = lines

    def set_device_statuses(self, devices: tuple[DeviceStatus, ...]) -> None:
        """Store live device titles and rebuild only when menu shape changes."""

        current_statuses, current_firmware = self._device_menu_state
        labels = tuple(device.address or device.label for device in devices)
        statuses = {device.address or device.label: device for device in devices}
        shape_changed = labels != tuple(current_statuses) or any(
            status.address != current_statuses[label].address or status.session is not current_statuses[label].session
            for label, status in statuses.items() if label in current_statuses
        )
        if not shape_changed:
            for label, status in statuses.items():
                current_statuses[label] = status
            return
        firmware_actions = {
            label: current_firmware.get(label, (False, tr("unavailable")))
            if label in current_statuses
            else (False, tr("unavailable"))
            for label in labels
        }
        self._device_menu_state = (statuses, firmware_actions)
        self._menu_stale = True
        self._icon.request_publish()

    def set_firmware_action(
        self, label: str, enabled: bool, detail: str
    ) -> None:
        device_statuses, firmware_actions = self._device_menu_state
        if label in device_statuses:
            firmware_actions[label] = (enabled, detail)

    def set_bridge_update_action(self, enabled: bool, detail: str) -> None:
        self._bridge_update_action = (enabled, detail)

    def stop(self) -> None:
        self._icon.stop()
