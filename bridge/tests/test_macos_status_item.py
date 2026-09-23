"""The NSStatusItem shell, driven through fake AppKit bindings.

These run on any platform: the point is to pin the behaviour that a menu bar
cannot show us — which artwork each state uses, that the idle state is dimming
rather than a third file, that AppKit is only ever touched on the main thread,
and that action targets stay alive.
"""

from __future__ import annotations

import unittest
from unittest.mock import Mock
from pathlib import Path

from quotaframe_bridge.i18n import tr
from quotaframe_bridge.ui.macos import icons
from quotaframe_bridge.ui.macos.menu_model import (
    FORGET_LABEL,
    MenuAction,
    build_menu,
)
from quotaframe_bridge.ui.macos.status_item import (
    AppKitBindings,
    MacStatusItemShell,
    StatusItemError,
)
from quotaframe_bridge.ui.status import TrayState


class FakeImage:
    def __init__(self, path: Path) -> None:
        self.path = path
        self.size = None
        self.template = False

    def setSize_(self, size) -> None:
        self.size = size

    def setTemplate_(self, value: bool) -> None:
        self.template = value


class FakeMenuItem:
    def __init__(self) -> None:
        self.title = None
        self.enabled = None
        self.target = None
        self.action = None
        self.state = None
        self.separator = False

    def setTitle_(self, title) -> None:
        self.title = title

    def setEnabled_(self, value) -> None:
        self.enabled = value

    def setTarget_(self, target) -> None:
        self.target = target

    def setAction_(self, action) -> None:
        self.action = action

    def setState_(self, state) -> None:
        self.state = state


class FakeSeparator(FakeMenuItem):
    def __init__(self) -> None:
        super().__init__()
        self.separator = True


class FakeMenu:
    def __init__(self) -> None:
        self.items: list[FakeMenuItem] = []

    def setAutoenablesItems_(self, value) -> None:
        self.autoenables = value

    def removeAllItems(self) -> None:
        self.items.clear()

    def addItem_(self, item) -> None:
        self.items.append(item)


class FakeButton:
    def __init__(self) -> None:
        self.image = None
        self.appears_disabled = None

    def setImage_(self, image) -> None:
        self.image = image

    def setAppearsDisabled_(self, value) -> None:
        self.appears_disabled = value


class FakeStatusItem:
    def __init__(self) -> None:
        self._button = FakeButton()
        self.menu = None
        self.set_menu_calls = 0

    def button(self):
        return self._button

    def setMenu_(self, menu) -> None:
        self.menu = menu
        self.set_menu_calls += 1


class FakeStatusBar:
    def __init__(self, *, item: FakeStatusItem | None = None) -> None:
        self.item = FakeStatusItem() if item is None else item
        self.removed: list[object] = []

    def statusItemWithLength_(self, length):
        return self.item

    def removeStatusItem_(self, item) -> None:
        self.removed.append(item)


class Harness:
    """A shell wired to fakes, with the calls it made recorded."""

    def __init__(self, *, inline_dispatch: bool = True, status_bar=None) -> None:
        self.status_bar = FakeStatusBar() if status_bar is None else status_bar
        self.images: list[FakeImage] = []
        self.dispatched: list[object] = []
        self.events: list[tuple[str, object]] = []
        self.inline_dispatch = inline_dispatch

        def make_image(path: Path) -> FakeImage:
            image = FakeImage(path)
            self.images.append(image)
            return image

        def dispatch(work) -> None:
            self.dispatched.append(work)
            if self.inline_dispatch:
                work()

        def make_action_target(handler):
            return handler, "invoke:"

        self.bindings = AppKitBindings(
            status_bar=self.status_bar,
            variable_length=-1,
            make_image=make_image,
            make_menu=FakeMenu,
            make_menu_item=lambda title: FakeMenuItem(),
            make_separator=FakeSeparator,
            make_action_target=make_action_target,
            dispatch_to_main=dispatch,
            install_interactions=Mock(return_value=Mock()),
        )
        self.shell = MacStatusItemShell(
            on_refresh=lambda: self.events.append(("refresh", None)),
            on_firmware_update=lambda label: self.events.append(("firmware", label)),
            on_bridge_update=lambda: self.events.append(("bridge_update", None)),
            on_repair=lambda: self.events.append(("repair", None)),
            on_toggle_autostart=lambda v: self.events.append(("autostart", v)),
            on_open_log=lambda: self.events.append(("log", None)),
            on_quit=lambda: self.events.append(("quit", None)),
            bindings=self.bindings,
        )

    @property
    def button(self) -> FakeButton:
        return self.status_bar.item.button()

    def menu_titles(self) -> list[str]:
        return [
            item.title
            for item in self.status_bar.item.menu.items
            if not item.separator
        ]


class ArtworkTests(unittest.TestCase):
    def test_two_files_cover_three_states(self) -> None:
        harness = Harness()

        self.assertEqual(len(harness.images), 2)
        self.assertEqual(
            {image.path.name for image in harness.images},
            {icons.NORMAL_FILENAME, icons.ATTENTION_FILENAME},
        )

    def test_both_images_are_marked_as_templates(self) -> None:
        """Without this macOS never inverts them for a dark menu bar."""

        harness = Harness()

        self.assertTrue(all(image.template for image in harness.images))

    def test_images_are_sized_in_points(self) -> None:
        harness = Harness()

        for image in harness.images:
            self.assertEqual(image.size, (icons.ICON_POINTS, icons.ICON_POINTS))

    def test_warn_switches_artwork(self) -> None:
        harness = Harness()

        harness.shell.set_state(TrayState.WARN)

        self.assertEqual(harness.button.image.path.name, icons.ATTENTION_FILENAME)
        self.assertFalse(harness.button.appears_disabled)

    def test_idle_dims_the_normal_artwork_instead_of_loading_a_third_file(
        self,
    ) -> None:
        harness = Harness()

        harness.shell.set_state(TrayState.IDLE)

        self.assertEqual(harness.button.image.path.name, icons.NORMAL_FILENAME)
        self.assertTrue(harness.button.appears_disabled)

    def test_ok_is_the_undimmed_normal_artwork(self) -> None:
        harness = Harness()

        harness.shell.set_state(TrayState.OK)

        self.assertEqual(harness.button.image.path.name, icons.NORMAL_FILENAME)
        self.assertFalse(harness.button.appears_disabled)

    def test_missing_artwork_is_reported_clearly(self) -> None:
        def missing(_path: Path) -> None:
            return None

        with self.assertRaises(icons.IconError):
            icons.load_images(missing, directory=Path("/nonexistent"))


class MenuTests(unittest.TestCase):
    def test_menu_matches_the_model(self) -> None:
        harness = Harness()

        expected = [item.label for item in build_menu() if not item.separator]
        self.assertEqual(harness.menu_titles(), expected)
        self.assertIn(tr("check_bridge_update"), harness.menu_titles())

    def test_information_rows_are_disabled_and_actions_are_not(self) -> None:
        harness = Harness()
        items = [i for i in harness.status_bar.item.menu.items if not i.separator]

        self.assertFalse(items[0].enabled)
        self.assertFalse(items[1].enabled)
        # The remove-device row is the one action that starts disabled: a
        # shell with no adopted devices has nothing to remove.
        forget = next(i for i in items if i.title == FORGET_LABEL)
        self.assertFalse(forget.enabled)
        self.assertTrue(
            all(item.enabled for item in items[2:] if item is not forget)
        )

    def test_remove_device_row_enables_once_a_device_is_owned(self) -> None:
        harness = Harness()

        harness.shell.set_devices((("M5", "M5", None),))
        items = [i for i in harness.status_bar.item.menu.items if not i.separator]

        self.assertTrue(
            next(i for i in items if i.title == FORGET_LABEL).enabled
        )

    def test_selecting_a_row_reaches_its_handler(self) -> None:
        harness = Harness()
        items = [i for i in harness.status_bar.item.menu.items if not i.separator]

        for item in items[2:]:
            item.target()

        self.assertEqual(
            [name for name, _ in harness.events],
            ["refresh", "bridge_update", "repair", "autostart", "log", "quit"],
        )

    def test_bridge_update_row_changes_in_place(self) -> None:
        harness = Harness()

        harness.shell.set_bridge_update_action(False, tr("checking"))
        checking = next(
            item
            for item in harness.status_bar.item.menu.items
            if item.title == tr("checking")
        )
        self.assertFalse(checking.enabled)

        harness.shell.set_bridge_update_action(True, tr("download_version", version='v0.2.0'))
        available = next(
            item
            for item in harness.status_bar.item.menu.items
            if item.title == tr("download_version", version='v0.2.0')
        )
        self.assertIs(available, checking)
        self.assertTrue(available.enabled)
        self.assertEqual(harness.status_bar.item.set_menu_calls, 1)

    def test_autostart_toggle_sends_the_opposite_of_the_current_value(self) -> None:
        harness = Harness()
        harness.shell.set_autostart_checked(True)
        toggle = next(
            item
            for item in harness.status_bar.item.menu.items
            if item.title and item.state is not None
        )

        toggle.target()

        self.assertEqual(harness.events, [("autostart", False)])

    def test_autostart_check_is_rendered_as_menu_item_state(self) -> None:
        harness = Harness()

        harness.shell.set_autostart_checked(True)

        toggle = next(
            item
            for item in harness.status_bar.item.menu.items
            if item.state is not None
        )
        self.assertEqual(toggle.state, 1)

    def test_info_lines_rebuild_the_menu(self) -> None:
        harness = Harness()

        harness.shell.set_info_lines(("M5 已连接", "Codex 42% · Claude 17%"))

        self.assertEqual(harness.menu_titles()[:2], ["M5 已连接", "Codex 42% · Claude 17%"])

    def test_the_menu_is_never_replaced_after_startup(self) -> None:
        """Replacing an open menu stacks its rows; see _build_menu."""

        harness = Harness()

        for percent in (11, 22, 33):
            harness.shell.set_info_lines(("M5 已连接", f"Codex {percent}%"))
        harness.shell.set_autostart_checked(True)

        self.assertEqual(harness.status_bar.item.set_menu_calls, 1)
        self.assertEqual(harness.menu_titles()[1], "Codex 33%")

    def test_an_unchanged_snapshot_touches_nothing(self) -> None:
        """The controller republishes every five seconds regardless."""

        harness = Harness(inline_dispatch=False)
        lines = ("M5 已连接", "Codex 42% · Claude 17%")
        harness.shell.set_info_lines(lines)
        before = len(harness.dispatched)

        harness.shell.set_info_lines(lines)

        self.assertEqual(len(harness.dispatched), before)

    def test_action_targets_are_retained(self) -> None:
        """Objective-C holds a menu item's target weakly."""

        harness = Harness()

        self.assertEqual(len(harness.shell._targets), 11)


class ThreadingTests(unittest.TestCase):
    def test_every_mutation_goes_through_the_main_thread_dispatcher(self) -> None:
        harness = Harness(inline_dispatch=False)
        before = len(harness.dispatched)

        harness.shell.set_state(TrayState.WARN)
        harness.shell.set_info_lines(("a", "b"))
        harness.shell.set_autostart_checked(True)
        harness.shell.set_bridge_update_action(False, tr("checking"))
        harness.shell.stop()

        self.assertEqual(len(harness.dispatched) - before, 5)

    def test_nothing_is_applied_before_the_dispatcher_runs_it(self) -> None:
        harness = Harness(inline_dispatch=False)

        harness.shell.set_state(TrayState.WARN)

        # Still the artwork chosen during construction.
        self.assertEqual(harness.button.image.path.name, icons.NORMAL_FILENAME)
        for work in harness.dispatched:
            work()
        self.assertEqual(harness.button.image.path.name, icons.ATTENTION_FILENAME)


class IgnoredSurfaceTests(unittest.TestCase):
    def test_tooltip_is_ignored_because_menu_bar_items_have_none(self) -> None:
        harness = Harness()

        harness.shell.set_tooltip("QuotaFrame · M5 已连接")

        self.assertEqual(harness.dispatched, [])

    def test_old_firmware_menu_cannot_route_to_re_adopted_address(self):
        harness = Harness(inline_dispatch=True)
        shell = harness.shell
        shell.set_devices((("AA", "Panel", object()),))
        shell.set_firmware_action("AA", True, "v1.0.0")
        old = next(item for item in shell._menu.items if item.title and item.title.startswith("Panel ·"))
        shell.set_devices((("AA", "Panel", object()),))
        old.target()
        self.assertEqual(harness.events, [])
        current = next(item for item in shell._menu.items if item.title and item.title.startswith("Panel ·"))
        current.target()
        self.assertEqual(harness.events, [("firmware", "AA")])

    def test_firmware_progress_and_device_changes_keep_native_menu_and_routing(self) -> None:
        harness = Harness(inline_dispatch=False)
        shell = harness.shell
        menu = shell._menu
        shell.set_devices((("M5", "M5", None), ("WS", "WS", None)))
        shell.set_firmware_action("M5", True, "v1.0.0")
        shell.set_firmware_action("WS", False, "42%")
        self.assertFalse(any("42%" in title for title in harness.menu_titles()))
        for work in harness.dispatched:
            work()
        harness.dispatched.clear()
        self.assertIs(shell._menu, menu)
        # Identify rows by device label independently of the selected language.
        rows = [item for item in menu.items if item.title and item.title.startswith(("M5 ·", "WS ·"))]
        self.assertEqual([item.enabled for item in rows], [True, False])
        rows[0].target()
        self.assertEqual(harness.events, [("firmware", "M5")])
        shell.set_devices((("WS", "WS", None),))
        shell.set_devices((("WS", "WS", None), ("M5", "M5", None)))
        for work in harness.dispatched:
            work()
        self.assertIs(shell._menu, menu)
        m5 = next(item for item in menu.items if item.title and item.title.startswith("M5 ·"))
        self.assertFalse(m5.enabled)
        self.assertNotIn("v1.0.0", m5.title)
        self.assertFalse(menu.autoenables)

    def test_notification_delivery_failure_never_escapes_the_shell(self) -> None:
        harness = Harness()

        class RaisingNotifications:
            def __init__(self) -> None:
                self.calls: list[tuple[str, str]] = []

            def notify(self, title: str, body: str) -> bool:
                self.calls.append((title, body))
                raise RuntimeError("framework unavailable")

        notifications = RaisingNotifications()
        harness.shell._notifications = notifications

        with self.assertLogs(
            "quotaframe_bridge.ui.macos.status_item", level="WARNING"
        ):
            harness.shell.notify(tr("bridge_available"), "v0.2.0")

        self.assertEqual(
            notifications.calls,
            [(tr("bridge_available"), "v0.2.0")],
        )


class LifecycleTests(unittest.TestCase):
    def test_stop_removes_the_status_item(self) -> None:
        harness = Harness()

        harness.shell.stop()

        self.assertEqual(harness.status_bar.removed, [harness.status_bar.item])

    def test_refused_status_item_is_reported(self) -> None:
        class RefusingBar(FakeStatusBar):
            def statusItemWithLength_(self, length):
                return None

        with self.assertRaises(StatusItemError):
            Harness(status_bar=RefusingBar())


if __name__ == "__main__":
    unittest.main()
