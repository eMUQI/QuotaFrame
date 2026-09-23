from __future__ import annotations

import sys
import unittest
from unittest.mock import Mock, patch

if sys.platform != "win32":
    raise unittest.SkipTest("the tray shell reads the Windows taskbar theme")


from quotaframe_bridge.i18n import tr
from quotaframe_bridge.ui.pystray_shell import (
    WM_ENDSESSION, WM_QUERYENDSESSION, WM_RBUTTONUP, PystrayShell,
)
from quotaframe_bridge.ui.status import DeviceStatus, TrayState


def make_shell(**overrides: object) -> PystrayShell:
    arguments: dict[str, object] = {
        "on_refresh": lambda: None,
        "on_bridge_update": lambda: None,
        "on_add_device": lambda: None,
        "on_repair": lambda _label: None,
        "on_firmware_update": lambda _label: None,
        "on_forget_device": lambda _label: None,
        "on_toggle_autostart": lambda _checked: None,
        "on_open_log": lambda: None,
        "on_quit": lambda: None,
        "device_labels": ("M5", "WS"),
    }
    arguments.update(overrides)
    return PystrayShell(**arguments)  # type: ignore[arg-type]


class PystrayShellTests(unittest.TestCase):
    def test_restart_manager_query_cancel_and_confirmed_shutdown(self):
        quit_app = Mock()
        shell = make_shell(on_quit=quit_app)
        self.addCleanup(lambda: [icon.close() for icon in shell._icons.values()])
        handlers = shell._icon._message_handlers
        self.assertEqual(handlers[WM_QUERYENDSESSION](0, 1), 1)
        quit_app.assert_not_called()
        self.assertEqual(handlers[WM_ENDSESSION](0, 1), 0)
        quit_app.assert_not_called()
        self.assertEqual(handlers[WM_ENDSESSION](1, 1), 0)
        quit_app.assert_called_once_with()

    def test_ota_veto_only_applies_to_automatic_exit(self):
        manual_quit, auto_quit = Mock(), Mock()
        can_quit = Mock(return_value=False)
        shell = make_shell(on_quit=manual_quit, on_auto_quit=auto_quit, can_auto_quit=can_quit)
        self.addCleanup(lambda: [icon.close() for icon in shell._icons.values()])
        handlers = shell._icon._message_handlers
        self.assertEqual(handlers[WM_QUERYENDSESSION](0, 1), 0)
        self.assertTrue(shell._build_menu().items[-1].enabled)
        shell._on_quit()
        manual_quit.assert_called_once_with()
        handlers[WM_ENDSESSION](0, 1)
        auto_quit.assert_not_called()
        can_quit.return_value = True
        self.assertEqual(handlers[WM_QUERYENDSESSION](0, 1), 1)
        handlers[WM_ENDSESSION](1, 1)
        auto_quit.assert_called_once_with()
        manual_quit.assert_called_once_with()

    def test_notification_click_opens_current_menu_without_running_an_action(self):
        from quotaframe_bridge.ui.pystray_shell import _MarshalledIcon, NIN_BALLOONUSERCLICK
        from unittest.mock import Mock

        download = Mock()
        toggle = Mock()
        shell = make_shell(on_bridge_update=download, on_toggle_screensaver=toggle)
        self.addCleanup(lambda: [icon.close() for icon in shell._icons.values()])
        with patch.object(shell._icon, "_update_menu") as rebuild, patch.object(
            _MarshalledIcon.__bases__[0], "_on_notify"
        ) as native:
            shell._icon._on_notify(0, NIN_BALLOONUSERCLICK)

        rebuild.assert_called_once()
        native.assert_called_once_with(0, WM_RBUTTONUP)
        download.assert_not_called()
        toggle.assert_not_called()

    def test_wheel_uses_the_registered_notification_icon_id(self):
        import pystray
        from pystray._util import win32
        shell = make_shell()
        self.addCleanup(lambda: [icon.close() for icon in shell._icons.values()])
        registered = []
        with patch.object(win32, "Shell_NotifyIcon", side_effect=lambda code, data: registered.append(data.uID)):
            shell._icon._message(win32.NIM_ADD, 0)
        with patch.object(pystray.Icon, "_run"), patch(
            "quotaframe_bridge.ui.windows_wheel.TrayWheelHook"
        ) as hook:
            shell._icon._run()
        self.assertEqual(hook.call_args.args[1], registered[0])
        hook.return_value.start.assert_called_once()
        hook.return_value.stop.assert_called_once()

    def test_old_menu_action_keeps_its_address_after_name_changes(self):
        selected = []
        shell = make_shell(on_forget_device=selected.append, device_labels=())
        self.addCleanup(lambda: [icon.close() for icon in shell._icons.values()])
        shell.set_device_statuses((DeviceStatus("M5", True, True, address="AA01"),))
        shell._publish()
        device_menu = next(item for item in shell._icon.menu if item.submenu is not None)
        forget = next(item for item in device_menu.submenu if item.text == tr("forget_menu"))
        shell.set_device_statuses((DeviceStatus("M5 renamed", True, True, address="AA01"),
                                   DeviceStatus("M5", True, True, address="AA02")))
        forget(shell._icon)
        self.assertEqual(selected, ["AA01"])

    def test_run_detaches_the_tray_message_loop_for_tk(self) -> None:
        """Tk owns the main thread, so pystray must use its integration API."""

        shell = make_shell()
        calls: list[str] = []
        shell._icon.run_detached = lambda _setup: calls.append("detached")  # type: ignore[method-assign]

        try:
            shell.run()
            self.assertEqual(calls, ["detached"])
        finally:
            for icon in shell._icons.values():
                icon.close()


class PystrayThreadAffinityTests(unittest.TestCase):
    """Every Win32 call has to happen on the thread owning the tray window."""

    def setUp(self) -> None:
        self.shell = make_shell()
        self.addCleanup(
            lambda: [icon.close() for icon in self.shell._icons.values()]
        )
        self.rebuilds: list[str] = []
        self.shell._icon._update_menu = lambda: self.rebuilds.append("menu")  # type: ignore[method-assign]

    def test_publishing_status_never_rebuilds_the_menu_off_thread(self) -> None:
        """A freed menu handle is one the click handler is about to display."""

        self.shell.set_info_lines(("M5 已连接", "Codex 42%"))
        self.shell.set_autostart_checked(True)
        self.shell.set_state(TrayState.OK)
        self.shell.set_tooltip("QuotaFrame")

        self.assertEqual(self.rebuilds, [])

    def test_the_click_handler_rebuilds_the_menu_before_showing_it(self) -> None:
        """Dynamic rows are read once, when Windows builds the native menu."""

        self.shell._icon._on_notify(0, WM_RBUTTONUP)

        self.assertEqual(self.rebuilds, ["menu"])

    def test_requesting_a_refresh_before_the_window_exists_is_a_no_op(self) -> None:
        """Status arrives from the worker before pystray has created its hwnd."""

        self.shell.set_state(TrayState.WARN)

        self.assertIsNone(self.shell._icon._hwnd)

    def test_publish_applies_every_stored_value_at_once(self) -> None:
        """This is the body that runs on the icon thread once it is poked."""

        applied: list[tuple[str, object]] = []
        self.shell._icon._update_icon = lambda: applied.append(("icon", None))  # type: ignore[method-assign]
        self.shell._icon._update_title = lambda: applied.append(("title", None))  # type: ignore[method-assign]
        self.shell._icon._notify = lambda body, title: applied.append(  # type: ignore[method-assign]
            ("notify", (title, body))
        )
        self.shell._icon._visible = True

        self.shell.set_state(TrayState.WARN)
        self.shell.set_tooltip("QuotaFrame · WS 已断开")
        self.shell.notify(tr("panel_disconnected"), tr("panel_disconnected_body", device='WS'))
        self.shell._publish()

        self.assertEqual(self.shell._icon.title, "QuotaFrame · WS 已断开")
        self.assertIs(self.shell._icon.icon, self.shell._icons[TrayState.WARN])
        self.assertIn(
            ("notify", (tr("panel_disconnected"), tr("panel_disconnected_body", device='WS'))),
            applied,
        )

    def test_a_balloon_queued_before_the_icon_runs_is_not_lost(self) -> None:
        """The startup-failure balloon fires before the tray is up."""

        self.shell.notify(tr("startup_failed"), tr("startup_failed_body"))
        sent: list[tuple[str, str]] = []
        self.shell._icon._notify = lambda body, title: sent.append((title, body))  # type: ignore[method-assign]
        self.shell._icon._visible = True

        self.shell._publish()

        self.assertEqual(sent, [(tr("startup_failed"), tr("startup_failed_body"))])

    def test_information_rows_read_as_starting_before_the_first_snapshot(self) -> None:
        """Empty placeholders render as two blank rows, which reads as a fault."""

        shell = make_shell()

        try:
            rows = [item.text for item in shell._icon.menu][:2]
            self.assertEqual(rows, [tr("starting_devices"), tr("starting_usage")])
        finally:
            for icon in shell._icons.values():
                icon.close()

    def test_each_device_appears_once_with_its_actions(self) -> None:
        repaired: list[str] = []
        forgotten: list[str] = []
        shell = make_shell(
            on_repair=repaired.append,
            on_forget_device=forgotten.append,
            device_labels=("M5StickS3-A", "M5StickS3-B"),
        )

        try:
            shell.set_device_statuses(
                (
                    DeviceStatus("M5StickS3-A", True, True),
                    DeviceStatus("M5StickS3-B", False, True),
                )
            )
            top_level = list(shell._icon.menu)
            device_items = [
                item for item in top_level if item.text.startswith("M5StickS3-")
            ]

            self.assertEqual(
                [item.text for item in device_items],
                ["M5StickS3-A · " + tr("connected"), "M5StickS3-B · " + tr("disconnected")],
            )
            self.assertFalse(
                {"设备固件更新", tr("repair"), tr("forget")}
                & {item.text for item in top_level}
            )
            for device_item in device_items:
                submenu = list(device_item.submenu or ())
                self.assertEqual(
                    [item.text for item in submenu],
                    [tr("firmware_detail", detail=tr("unavailable")), tr("repair_menu"), tr("forget_menu")],
                )
                submenu[1](shell._icon)
                submenu[2](shell._icon)
            self.assertEqual(repaired, ["M5StickS3-A", "M5StickS3-B"])
            self.assertEqual(forgotten, ["M5StickS3-A", "M5StickS3-B"])
        finally:
            for icon in shell._icons.values():
                icon.close()

    def test_status_titles_refresh_without_an_off_thread_menu_rebuild(self) -> None:
        self.shell.set_device_statuses(
            (
                DeviceStatus("M5", True, True),
                DeviceStatus("WS", False, False),
            )
        )
        before = [
            item.text
            for item in self.shell._icon.menu
            if item.submenu is not None
        ]

        self.shell.set_device_statuses(
            (
                DeviceStatus("M5", False, True),
                DeviceStatus("WS", True, True),
            )
        )
        after = [
            item.text
            for item in self.shell._icon.menu
            if item.submenu is not None
        ]

        self.assertEqual(before, ["M5 · " + tr("connected"), "WS · " + tr("not_found")])
        self.assertEqual(after, ["M5 · " + tr("disconnected"), "WS · " + tr("connected")])
        self.assertEqual(self.rebuilds, [])

    def test_removed_device_old_menu_stays_readable_until_rebuilt(self) -> None:
        self.shell.set_device_statuses(
            (
                DeviceStatus("M5", True, True),
                DeviceStatus("WS", False, True),
            )
        )
        self.shell.set_firmware_action("WS", True, tr("firmware_version_available", version='1.0.0'))
        old_menu = self.shell._icon.menu

        self.shell.set_device_statuses((DeviceStatus("M5", True, True),))

        self.assertEqual(
            [item.text for item in old_menu if item.submenu is not None],
            ["M5 · " + tr("connected"), "WS · " + tr("disconnected")],
        )
        firmware_rows = {
            item.text.split(" · ", 1)[0]: list(item.submenu or ())[0]
            for item in old_menu
            if item.submenu is not None
        }
        self.assertEqual(firmware_rows["WS"].text, tr("firmware_detail", detail=tr("firmware_version_available", version="1.0.0")))
        self.assertTrue(firmware_rows["WS"].enabled)

    def test_readded_label_does_not_inherit_removed_firmware_action(self) -> None:
        self.shell.set_device_statuses(
            (
                DeviceStatus("M5", True, True),
                DeviceStatus("WS", False, True),
            )
        )
        self.shell.set_firmware_action("WS", True, tr("firmware_version_available", version='1.0.0'))
        self.shell.set_device_statuses((DeviceStatus("M5", True, True),))
        self.shell._publish()

        self.shell.set_device_statuses(
            (
                DeviceStatus("M5", True, True),
                DeviceStatus("WS", False, False),
            )
        )
        self.shell._publish()

        ws_item = next(
            item for item in self.shell._icon.menu if item.text.startswith("WS ·")
        )
        firmware = list(ws_item.submenu or ())[0]
        self.assertEqual(firmware.text, tr("firmware_detail", detail=tr("unavailable")))
        self.assertFalse(firmware.enabled)


    def test_adding_and_removing_devices_rebuilds_the_menu_shape(self) -> None:
        shell = make_shell(device_labels=())

        try:
            shell.set_device_statuses((DeviceStatus("M5", True, True),))
            shell._publish()
            self.assertEqual(
                [item.text for item in shell._icon.menu if item.submenu is not None],
                ["M5 · " + tr("connected")],
            )

            shell.set_device_statuses(())
            shell._publish()
            self.assertFalse(
                any(item.submenu is not None for item in shell._icon.menu)
            )
        finally:
            for icon in shell._icons.values():
                icon.close()

    def test_zero_devices_has_no_empty_device_submenu(self) -> None:
        shell = make_shell(device_labels=())

        try:
            self.assertFalse(
                any(item.submenu is not None for item in shell._icon.menu)
            )
        finally:
            for icon in shell._icons.values():
                icon.close()

    def test_add_device_row_invokes_callback(self) -> None:
        selected: list[str] = []
        shell = make_shell(on_add_device=lambda: selected.append("add"))

        try:
            item = next(
                item for item in shell._icon.menu if item.text == tr("add_device")
            )
            item(shell._icon)
            self.assertEqual(selected, ["add"])
        finally:
            for icon in shell._icons.values():
                icon.close()

    def test_device_firmware_row_tracks_progress_and_target(self) -> None:
        updated: list[str] = []
        shell = make_shell(on_firmware_update=updated.append)
        self.addCleanup(lambda: [icon.close() for icon in shell._icons.values()])

        shell.set_firmware_action("M5", True, tr("firmware_version_available", version='0.3.0'))
        shell.set_firmware_action("WS", False, tr("update_progress", percent='42'))
        device_items = {
            item.text.split(" · ", 1)[0]: item
            for item in shell._icon.menu
            if item.submenu is not None
        }
        m5_firmware = list(device_items["M5"].submenu or ())[0]
        ws_firmware = list(device_items["WS"].submenu or ())[0]

        self.assertEqual(m5_firmware.text, tr("firmware_detail", detail=tr("firmware_version_available", version="0.3.0")))
        self.assertEqual(ws_firmware.text, tr("firmware_detail", detail=tr("update_progress", percent=42)))
        self.assertTrue(m5_firmware.enabled)
        self.assertFalse(ws_firmware.enabled)
        m5_firmware(shell._icon)
        self.assertEqual(updated, ["M5"])

    def test_bridge_update_row_tracks_state_and_invokes_callback(self) -> None:
        selected: list[str] = []
        shell = make_shell(on_bridge_update=lambda: selected.append("update"))
        self.addCleanup(lambda: [icon.close() for icon in shell._icons.values()])

        initial = next(
            item for item in shell._icon.menu if item.text == tr("check_bridge_update")
        )
        self.assertTrue(initial.enabled)

        shell.set_bridge_update_action(False, tr("checking"))
        checking = next(item for item in shell._icon.menu if item.text == tr("checking"))
        self.assertFalse(checking.enabled)

        shell.set_bridge_update_action(True, tr("download_version", version='v0.2.0'))
        available = next(
            item for item in shell._icon.menu if item.text == tr("download_version", version='v0.2.0')
        )
        self.assertTrue(available.enabled)
        available(shell._icon)
        self.assertEqual(selected, ["update"])


class ScreenToggleClickTests(unittest.TestCase):
    def test_left_click_toggles_once_and_right_click_keeps_menu(self) -> None:
        from quotaframe_bridge.ui.pystray_shell import WM_LBUTTONUP, _MarshalledIcon
        clicks = []
        shell = make_shell(on_toggle_screensaver=lambda: clicks.append("toggle"))
        with patch.object(_MarshalledIcon.__bases__[0], "_on_notify") as native:
            with patch.object(shell._icon, "_update_menu") as menu:
                shell._icon._on_notify(0, WM_LBUTTONUP)
                self.assertEqual(clicks, ["toggle"])
                native.assert_not_called()
                menu.assert_not_called()
                shell._icon._on_notify(0, WM_RBUTTONUP)
                menu.assert_called_once()
                native.assert_called_once_with(0, WM_RBUTTONUP)
                self.assertEqual(clicks, ["toggle"])


if __name__ == "__main__":
    unittest.main()
