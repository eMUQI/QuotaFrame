"""Native status-item rendering and main-thread dispatch.

AppKit is late-bound through `AppKitBindings` for the same reason
`pairing/windows.py` late-binds WinRT: the module must import on any platform,
and the behaviour must be assertable without a menu bar.

Threading contract: every AppKit call here has to happen on the main thread,
and the controller calls in from the collection worker. Each public method
therefore stores what it was given and hands a closure to
`bindings.dispatch_to_main`. Nothing in this module touches AppKit from the
calling thread.
"""

from __future__ import annotations

import logging
from collections.abc import Callable
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from quotaframe_bridge.i18n import tr
from quotaframe_bridge.ui.macos import icons
from quotaframe_bridge.ui.macos.interactions import StatusItemInteractions
from quotaframe_bridge.ui.macos.menu_model import (
    MenuAction,
    MenuItem,
    STARTING_LINES,
    build_menu,
)
from quotaframe_bridge.ui.macos.native_notifications import (
    MacNotificationCenter,
)
from quotaframe_bridge.ui.status import TrayState

LOGGER = logging.getLogger(__name__)

ActionTargetFactory = Callable[[Callable[[], None]], "tuple[Any, str]"]


class StatusItemError(RuntimeError):
    """AppKit is unavailable or refused to create the status item."""


@dataclass(frozen=True)
class AppKitBindings:
    """Late-bound AppKit surface, so tests can supply fakes."""

    status_bar: Any
    variable_length: Any
    make_image: Callable[[Path], Any]
    make_menu: Callable[[], Any]
    make_menu_item: Callable[[str], Any]
    make_separator: Callable[[], Any]
    make_action_target: ActionTargetFactory
    dispatch_to_main: Callable[[Callable[[], None]], None]
    install_interactions: Callable[..., Any]


def load_appkit_bindings() -> AppKitBindings:
    """Load AppKit and wrap the handful of calls this module makes."""

    try:
        import AppKit
        import objc
    except ImportError as exc:
        raise StatusItemError(
            "the macOS menu bar requires pyobjc, which ships with Bleak on this "
            "platform"
        ) from exc

    class _ActionTarget(AppKit.NSObject):
        """An Objective-C object a menu item can send its action to."""

        def initWithHandler_(self, handler):
            self = objc.super(_ActionTarget, self).init()
            if self is None:
                return None
            self._handler = handler
            return self

        def invoke_(self, _sender) -> None:
            try:
                self._handler()
            except Exception:
                LOGGER.exception("menu action raised")

    def make_action_target(handler: Callable[[], None]) -> tuple[Any, str]:
        return _ActionTarget.alloc().initWithHandler_(handler), "invoke:"

    def make_image(path: Path) -> Any:
        return AppKit.NSImage.alloc().initWithContentsOfFile_(str(path))

    def dispatch_to_main(work: Callable[[], None]) -> None:
        AppKit.NSOperationQueue.mainQueue().addOperationWithBlock_(work)

    return AppKitBindings(
        status_bar=AppKit.NSStatusBar.systemStatusBar(),
        variable_length=AppKit.NSVariableStatusItemLength,
        make_image=make_image,
        make_menu=AppKit.NSMenu.alloc().init,
        make_menu_item=lambda title: AppKit.NSMenuItem.alloc().init(),
        make_separator=AppKit.NSMenuItem.separatorItem,
        make_action_target=make_action_target,
        dispatch_to_main=dispatch_to_main,
        install_interactions=lambda item, menu, toggle, page: StatusItemInteractions(
            AppKit, item, menu, make_action_target, toggle, page
        ),
    )


class MacStatusItemShell:
    """Drive an NSStatusItem. Contains no thresholds and no copy decisions."""

    def __init__(
        self,
        *,
        on_refresh: Callable[[], None],
        on_add_device: Callable[[], None] = lambda: None,
        on_repair: Callable[[], None],
        on_forget_device: Callable[[], None] = lambda: None,
        on_toggle_autostart: Callable[[bool], None],
        on_open_log: Callable[[], None],
        on_quit: Callable[[], None],
        on_firmware_update: Callable[[str], None] = lambda _label: None,
        on_bridge_update: Callable[[], None] = lambda: None,
        on_toggle_screensaver: Callable[[], None] = lambda: None,
        on_turn_page: Callable[[int], None] = lambda _direction: None,
        bindings: AppKitBindings | None = None,
        icon_directory: Path | None = None,
        notifications: MacNotificationCenter | None = None,
    ) -> None:
        self._bindings = load_appkit_bindings() if bindings is None else bindings
        self._images = icons.load_images(
            self._bindings.make_image,
            directory=icon_directory,
        )
        self._handlers: dict[MenuAction, Callable[[], None]] = {
            MenuAction.TOGGLE_SCREEN: on_toggle_screensaver,
            MenuAction.PREVIOUS_PAGE: lambda: on_turn_page(-1),
            MenuAction.NEXT_PAGE: lambda: on_turn_page(1),
            MenuAction.REFRESH: on_refresh,
            MenuAction.ADD_DEVICE: on_add_device,
            MenuAction.BRIDGE_UPDATE: on_bridge_update,
            MenuAction.REPAIR: on_repair,
            MenuAction.FORGET_DEVICE: on_forget_device,
            MenuAction.TOGGLE_AUTOSTART: self._toggle_autostart,
            MenuAction.OPEN_LOG: on_open_log,
            MenuAction.QUIT: on_quit,
        }
        self._on_firmware_update = on_firmware_update
        self._firmware_actions: dict[str, tuple[bool, str]] = {}
        self._on_toggle_autostart = on_toggle_autostart
        self._state = TrayState.IDLE
        self._info_lines: tuple[str, ...] = STARTING_LINES
        self._autostart_checked = False
        self._bridge_update_enabled = True
        self._bridge_update_detail = tr("check_bridge_update")
        self._devices: dict[str, tuple[str, object]] = {}
        self._notifications = (
            MacNotificationCenter() if notifications is None else notifications
        )
        # Targets outlive the menu items that point at them; Objective-C keeps
        # only a weak reference to a menu item's target.
        self._targets: list[Any] = []

        # Parallel to the entries build_menu() returns, so an update can find
        # the row it needs to retitle without touching the menu itself.
        self._menu_items: list[Any] = []

        self._item = self._bindings.status_bar.statusItemWithLength_(
            self._bindings.variable_length
        )
        if self._item is None:
            raise StatusItemError("macOS refused to create a status item")
        self._apply_state()
        self._build_menu()
        self._interactions = self._bindings.install_interactions(
            self._item, self._menu, on_toggle_screensaver, on_turn_page
        )

    # -- AppKit work, main thread only -----------------------------------

    def _apply_state(self) -> None:
        filename, _dimmed = icons.STATE_ARTWORK[self._state]
        button = self._item.button()
        if button is None:
            return
        button.setImage_(self._images[filename])
        # The third state is this flag rather than a third file: macOS dims the
        # button itself, which is exactly what "not ready yet" should look like.
        button.setAppearsDisabled_(icons.is_dimmed(self._state))

    def _build_menu(self) -> None:
        """Build persistent menu rows; status updates mutate them in place."""

        menu = self._bindings.make_menu()
        menu.setAutoenablesItems_(False)
        self._targets = []
        self._menu_items = []
        entries = build_menu(
            self._info_lines,
            autostart_checked=self._autostart_checked,
            bridge_update_enabled=self._bridge_update_enabled,
            bridge_update_detail=self._bridge_update_detail,
            forget_enabled=bool(self._devices),
            firmware_actions=self._firmware_rows(),
        )
        self._menu_keys = tuple((entry.action, entry.device_address, self._devices.get(entry.device_address, ("", None))[1]) for entry in entries)
        for entry in entries:
            item = self._menu_item(entry)
            self._menu_items.append(item)
            menu.addItem_(item)
        self._menu = menu
        self._item.setMenu_(menu)

    def _apply_menu(self) -> None:
        """Push the current labels onto the menu that already exists."""

        entries = build_menu(
            self._info_lines,
            autostart_checked=self._autostart_checked,
            bridge_update_enabled=self._bridge_update_enabled,
            bridge_update_detail=self._bridge_update_detail,
            forget_enabled=bool(self._devices),
            firmware_actions=self._firmware_rows(),
        )
        keys = tuple((entry.action, entry.device_address, self._devices.get(entry.device_address, ("", None))[1]) for entry in entries)
        if keys != self._menu_keys:
            self._menu.removeAllItems()
            self._targets = []
            self._menu_items = []
            for entry in entries:
                item = self._menu_item(entry)
                self._menu_items.append(item)
                self._menu.addItem_(item)
            self._menu_keys = keys
            return
        for entry, item in zip(entries, self._menu_items):
            if entry.separator:
                continue
            item.setTitle_(entry.label)
            item.setEnabled_(entry.enabled)
            if entry.is_checkbox:
                item.setState_(1 if entry.checked else 0)

    def _menu_item(self, entry: MenuItem) -> Any:
        if entry.separator:
            return self._bindings.make_separator()
        item = self._bindings.make_menu_item(entry.label)
        item.setTitle_(entry.label)
        if entry.action is None:
            # An information row. Disabling it is what greys it out; without an
            # action it would look enabled but do nothing when clicked.
            item.setEnabled_(False)
            return item
        if entry.action is MenuAction.FIRMWARE_UPDATE:
            session = self._devices.get(entry.device_address, ("", None))[1]
            def handler():
                if (entry.device_address in self._devices
                        and self._devices.get(entry.device_address, ("", None))[1] is session):
                    self._on_firmware_update(entry.device_address)
        else:
            handler = self._handlers[entry.action]
        target, selector = self._bindings.make_action_target(handler)
        self._targets.append(target)
        item.setTarget_(target)
        item.setAction_(selector)
        item.setEnabled_(entry.enabled)
        if entry.is_checkbox:
            item.setState_(1 if entry.checked else 0)
        return item

    def _toggle_autostart(self) -> None:
        self._on_toggle_autostart(not self._autostart_checked)

    # -- Called from the worker thread ------------------------------------

    def set_state(self, state: TrayState) -> None:
        self._state = state
        self._bindings.dispatch_to_main(self._apply_state)

    def set_tooltip(self, text: str) -> None:
        """Deliberately ignored.

        A menu bar item has no hover tooltip, so the Windows tooltip line has
        nowhere to go on macOS. The same information is already in the two
        information rows at the top of the menu.
        """

    def notify(self, title: str, body: str) -> None:
        try:
            delivered = self._notifications.notify(title, body)
        except Exception as exc:
            LOGGER.warning(
                "macOS notification adapter failed: %s",
                type(exc).__name__,
            )
            return
        if not delivered:
            LOGGER.warning("macOS notification was not delivered: %s", title)

    def set_autostart_checked(self, checked: bool) -> None:
        if checked == self._autostart_checked:
            return
        self._autostart_checked = checked
        self._bindings.dispatch_to_main(self._apply_menu)

    def set_info_lines(self, lines: tuple[str, ...]) -> None:
        # The controller republishes every five seconds whether or not
        # anything moved. Nothing below needs doing for an identical snapshot.
        if lines == self._info_lines:
            return
        self._info_lines = lines
        self._bindings.dispatch_to_main(self._apply_menu)

    def device_labels(self) -> tuple[str, ...]:
        """Return owned addresses in menu order."""

        return tuple(self._devices)

    def _firmware_rows(self) -> tuple[tuple[str, str, bool, str], ...]:
        return tuple(
            (address, name, *self._firmware_actions.get(address, (False, tr("unavailable"))))
            for address, (name, _session) in self._devices.items()
        )

    def dispatch(self, callback) -> None:
        self._bindings.dispatch_to_main(callback)

    def set_devices(self, devices: tuple[tuple[str, str, object], ...]) -> None:
        def apply():
            current = {address: (name, session) for address, name, session in devices}
            if (tuple(current) == tuple(self._devices)
                    and all(address in self._devices
                            and self._devices[address][0] == name
                            and self._devices[address][1] is session
                            for address, (name, session) in current.items())):
                return
            self._firmware_actions = {
                address: value for address, value in self._firmware_actions.items()
                if address in current and address in self._devices
                and self._devices[address][1] is current[address][1]
            }
            self._devices = current
            self._apply_menu()
        self.dispatch(apply)

    def set_firmware_action(self, label: str, enabled: bool, detail: str) -> None:
        session = self._devices.get(label, ("", None))[1]
        def apply() -> None:
            if label not in self._devices or self._devices.get(label, ("", None))[1] is not session:
                return
            value = (enabled, detail)
            if self._firmware_actions.get(label) == value:
                return
            self._firmware_actions[label] = value
            self._apply_menu()

        self._bindings.dispatch_to_main(apply)

    def set_bridge_update_action(self, enabled: bool, detail: str) -> None:
        value = (enabled, detail)
        if value == (self._bridge_update_enabled, self._bridge_update_detail):
            return
        self._bridge_update_enabled, self._bridge_update_detail = value
        self._bindings.dispatch_to_main(self._apply_menu)

    def stop(self) -> None:
        def remove() -> None:
            self._interactions.stop()
            self._bindings.status_bar.removeStatusItem_(self._item)

        self._bindings.dispatch_to_main(remove)
