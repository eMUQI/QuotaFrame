from __future__ import annotations

import asyncio
import os
import unittest
from types import SimpleNamespace
from unittest.mock import AsyncMock, Mock, patch

from quotaframe_bridge.i18n import tr
from quotaframe_bridge import __version__
from quotaframe_bridge.service.update import UpdateState
from quotaframe_bridge.ui.macos import app
from quotaframe_bridge.ui import update_presenter, firmware_actions
from quotaframe_bridge.versioning import SemVer


PAGE_URL = "https://github.com/eMUQI/QuotaFrame/releases/tag/v0.2.0"


class _RecordingShell:
    def dispatch(self, callback):
        callback()

    def __init__(self, **callbacks: object) -> None:
        self.callbacks = callbacks
        self.autostart_checked: bool | None = None
        self.bridge_update_actions: list[tuple[bool, str]] = []
        self.firmware_actions = []
        self.notifications: list[tuple[str, str]] = []

    def set_autostart_checked(self, checked: bool) -> None:
        self.autostart_checked = checked

    def set_bridge_update_action(self, enabled: bool, detail: str) -> None:
        self.bridge_update_actions.append((enabled, detail))

    def notify(self, title: str, body: str) -> None:
        self.notifications.append((title, body))

    def set_state(self, _state: object) -> None:
        return None

    def set_info_lines(self, _lines: tuple[str, ...]) -> None:
        return None

    def set_tooltip(self, _text: str) -> None:
        return None

    def set_firmware_action(
        self, _label: str, _enabled: bool, _detail: str
    ) -> None:
        self.firmware_actions.append((_label, _enabled, _detail))

    def stop(self) -> None:
        return None


class _RecordingMonitor:
    def __init__(self) -> None:
        self.source: object | None = None
        self.current: SemVer | None = None
        self.presenter: object | None = None
        self.manual_checks: list[bool] = []
        self.run_started = asyncio.Event()
        self.run_error: Exception | None = None

    async def run(self) -> None:
        self.run_started.set()
        if self.run_error is not None:
            raise self.run_error

    async def check(self, *, manual: bool = False) -> None:
        self.manual_checks.append(manual)


class MenuBarUpdateWiringTests(unittest.IsolatedAsyncioTestCase):
    def make_application(
        self,
        *,
        monitor: _RecordingMonitor | None = None,
        source: object | None = None,
        browser=lambda _url: True,
    ) -> tuple[app.MenuBarApplication, _RecordingShell, _RecordingMonitor]:
        selected_monitor = _RecordingMonitor() if monitor is None else monitor
        selected_source = object() if source is None else source
        shell = _RecordingShell()

        def monitor_factory(actual_source, current, presenter):
            selected_monitor.source = actual_source
            selected_monitor.current = current
            selected_monitor.presenter = presenter
            return selected_monitor

        def make_shell(**callbacks: object) -> _RecordingShell:
            shell.callbacks = callbacks
            return shell

        with (
            patch.object(app, "MacDialogs", return_value=SimpleNamespace()),
            patch.object(app, "MacStatusItemShell", side_effect=make_shell),
            patch.object(app, "is_enabled", return_value=False),
        ):
            application = app.MenuBarApplication(
                release_source=selected_source,
                monitor_factory=monitor_factory,
                browser=browser,
            )
        return application, shell, selected_monitor

    async def test_firmware_update_delegates_to_shared_installation(self):
        application, shell, _ = self.make_application()
        device = SimpleNamespace(name="M5", address="device-uuid", update_task=None,
            manager=SimpleNamespace(connected=True, device_status=SimpleNamespace(
                capabilities={"ota.folder.v1"}, target="m5sticks3", firmware_version="0.1.0",
                firmware_project="quotaframe")),
            session=SimpleNamespace(exclusive_active=False))
        application._graph = SimpleNamespace(devices=[device], get=lambda address: device)
        async def install(graph, actual, url, confirm, progress):
            self.assertIs(actual, device)
            self.assertEqual(confirm, application._confirm_firmware)
            progress((2, 4))
            return SimpleNamespace(version="1.0.0")
        with patch.dict(os.environ, {firmware_actions.FIRMWARE_MANIFEST_ENV: PAGE_URL}), patch.object(firmware_actions, "install_firmware", side_effect=install):
            await application._firmware_update_async(device.address, expected=device)
        self.assertIn(("device-uuid", True, tr("firmware_cancel_progress", percent=50)), shell.firmware_actions)
        self.assertIn((tr("firmware_complete"), tr("firmware_complete_body", label="M5", version="1.0.0")), shell.notifications)
        self.assertEqual(application._firmware_updates, 0)

    async def test_repair_disables_firmware_action_and_busy_click_reports_status(self) -> None:
        application, shell, _ = self.make_application()
        application._firmware_manifest_url = lambda: PAGE_URL
        device = SimpleNamespace(
            name="M5", address="device-uuid", update_task=None, manager=SimpleNamespace(connected=True, device_status=None),
            session=SimpleNamespace(exclusive_active=True),
        )
        graph = SimpleNamespace(devices=[device], get=lambda _: device)
        application._graph = graph
        shell.set_firmware_action("M5", True, "v1.0.0")
        application._publish_firmware_actions(graph)
        self.assertEqual(shell.firmware_actions[-1], ("device-uuid", False, tr("updating")))
        await application._firmware_update_async("device-uuid", expected=graph.get("device-uuid"))
        self.assertEqual(shell.notifications[-1], (tr("firmware_failed"), tr("updating")))

    async def test_screen_callbacks_dispatch_to_worker_and_stop_safely(self) -> None:
        application, shell, _monitor = self.make_application()
        toggle = shell.callbacks["on_toggle_screensaver"]
        page = shell.callbacks["on_turn_page"]
        toggle()
        page(1)
        application._loop = Mock()
        application._service = Mock()
        toggle()
        page(-1)
        self.assertEqual(application._loop.call_soon_threadsafe.call_args_list, [
            unittest.mock.call(application._service.toggle_screensavers),
            unittest.mock.call(application._service.turn_pages, -1),
        ])
        application._stopping = True
        toggle()
        page(1)
        self.assertEqual(application._loop.call_soon_threadsafe.call_count, 2)

    async def test_startup_runs_monitor_without_delaying_ble_service(self) -> None:
        monitor = _RecordingMonitor()
        monitor.run_error = RuntimeError("private automatic failure")
        application, _shell, _monitor = self.make_application(monitor=monitor)
        service_started = asyncio.Event()

        class Service:
            async def run(self) -> None:
                service_started.set()
                await monitor.run_started.wait()

        async def publish_status(_graph: object) -> None:
            return None

        async def adopt(_graph: object) -> None:
            return None

        application._monitor_firmware_updates = AsyncMock()
        application._publish_status = publish_status  # type: ignore[method-assign]
        application._adopt_devices = adopt  # type: ignore[method-assign]
        with self.assertLogs(app.LOGGER, level="WARNING") as captured:
            await asyncio.wait_for(
                application._run_background_tasks(Service(), object()),
                timeout=0.5,
            )

        self.assertTrue(service_started.is_set())
        self.assertTrue(monitor.run_started.is_set())
        self.assertNotIn("private automatic failure", "\n".join(captured.output))

    async def test_menu_click_schedules_manual_check_on_worker_loop(self) -> None:
        application, shell, monitor = self.make_application()
        application._loop = asyncio.get_running_loop()

        callback = shell.callbacks["on_bridge_update"]
        callback()  # type: ignore[operator]
        for _ in range(10):
            if monitor.manual_checks:
                break
            await asyncio.sleep(0)

        self.assertEqual(monitor.manual_checks, [True])

    async def test_add_device_menu_runs_a_new_bounded_scan(self) -> None:
        application, shell, _monitor = self.make_application()
        application._loop = asyncio.get_running_loop()
        application._graph = object()  # type: ignore[assignment]

        with patch.object(app, "run_adoption", new=AsyncMock(return_value=())) as scan:
            callback = shell.callbacks["on_add_device"]
            callback()  # type: ignore[operator]
            for _ in range(10):
                if scan.await_count:
                    break
                await asyncio.sleep(0)

        scan.assert_awaited_once()

    async def test_forget_uses_the_address_shown_before_the_dialog(self) -> None:
        application, _shell, _monitor = self.make_application()
        application._loop = asyncio.get_running_loop()
        device = SimpleNamespace(name="M5", address="AA:BB:CC:DD:EE:01")
        application._graph = SimpleNamespace(  # type: ignore[assignment]
            names=(device.name,), devices=(device,)
        )

        class Dialogs:
            @staticmethod
            def choose_device_to_forget(labels: tuple[str, ...]) -> int:
                self.assertEqual(labels, ("M5",))
                device.name = "M5 ·EE01"
                return 0

        application._dialogs = Dialogs()  # type: ignore[assignment]
        application._forget_async = AsyncMock()  # type: ignore[method-assign]

        application._forget_device()
        for _ in range(10):
            if application._forget_async.await_count:  # type: ignore[union-attr]
                break
            await asyncio.sleep(0)

        application._forget_async.assert_awaited_once_with(  # type: ignore[union-attr]
            application._graph, "AA:BB:CC:DD:EE:01", expected=device
        )

    async def test_available_release_updates_menu_and_opens_only_page_url(self) -> None:
        opened: list[str] = []
        application, shell, monitor = self.make_application(
            browser=lambda url: opened.append(url) or True
        )
        assert monitor.presenter is not None
        monitor.presenter.set_update_state(  # type: ignore[attr-defined]
            UpdateState(True, tr("download_version", version='v0.2.0'), PAGE_URL)
        )

        callback = shell.callbacks["on_bridge_update"]
        callback()  # type: ignore[operator]

        self.assertEqual(shell.bridge_update_actions, [(True, tr("download_version", version='v0.2.0'))])
        self.assertEqual(opened, [PAGE_URL])
        self.assertEqual(monitor.manual_checks, [])

    async def test_browser_failure_notifies_without_exception_text(self) -> None:
        def fail(_url: str) -> bool:
            raise RuntimeError("private browser failure")

        _application, shell, monitor = self.make_application(browser=fail)
        assert monitor.presenter is not None
        monitor.presenter.set_update_state(  # type: ignore[attr-defined]
            UpdateState(True, tr("download_version", version='v0.2.0'), PAGE_URL)
        )

        with self.assertLogs(update_presenter.LOGGER, level="WARNING") as captured:
            callback = shell.callbacks["on_bridge_update"]
            callback()  # type: ignore[operator]

        rendered = " ".join(sum(shell.notifications, ()))
        self.assertIn(tr("open_download_failed"), rendered)
        self.assertNotIn("private browser failure", rendered)
        self.assertNotIn("private browser failure", "\n".join(captured.output))

    async def test_default_monitor_uses_configured_repository_and_version(self) -> None:
        source = object()
        monitor = _RecordingMonitor()
        shell = _RecordingShell()

        def monitor_factory(actual_source, current, presenter):
            monitor.source = actual_source
            monitor.current = current
            monitor.presenter = presenter
            return monitor

        def make_shell(**callbacks: object) -> _RecordingShell:
            shell.callbacks = callbacks
            return shell

        with (
            patch.object(app, "MacDialogs", return_value=SimpleNamespace()),
            patch.object(app, "MacStatusItemShell", side_effect=make_shell),
            patch.object(app, "is_enabled", return_value=False),
            patch.object(
                app, "release_repository", return_value="owner/product"
            ) as repository,
            patch.object(
                app, "ProductReleaseSource", return_value=source
            ) as source_type,
        ):
            app.MenuBarApplication(
                monitor_factory=monitor_factory,
                browser=lambda _url: True,
            )

        repository.assert_called_once_with(os.environ)
        source_type.assert_called_once_with("owner/product")
        self.assertIs(monitor.source, source)
        self.assertEqual(monitor.current, SemVer.parse(__version__))


if __name__ == "__main__":
    unittest.main()
