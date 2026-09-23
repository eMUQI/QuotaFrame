from __future__ import annotations

import asyncio
import ctypes
import io
import os
import sys
import threading
import unittest
from contextlib import redirect_stdout
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import AsyncMock, Mock, patch

from quotaframe_bridge.ui import firmware_actions
from quotaframe_bridge.i18n import tr
from quotaframe_bridge import __version__

if sys.platform == "win32":
    from quotaframe_bridge.ui import app
else:
    app = None  # type: ignore[assignment]
from quotaframe_bridge.protocol.messages import DeviceStatus
from quotaframe_bridge.protocol.ota_messages import OtaStatus
from quotaframe_bridge.service.firmware_update import (
    FirmwareUpdatePresentation,
)
from quotaframe_bridge.service.update import UpdateState
from quotaframe_bridge.ui.status import DeviceStatus as TrayDeviceStatus
from quotaframe_bridge.sources.firmware_release import FirmwareImage, FirmwareManifest
from quotaframe_bridge.sources.product_release import ProductRelease
from quotaframe_bridge.targets import TARGETS as MAINTAINED_TARGETS
from quotaframe_bridge.versioning import SemVer


PAGE_URL = "https://github.com/eMUQI/QuotaFrame/releases/tag/v0.2.0"
MANIFEST_URL = (
    "https://github.com/eMUQI/QuotaFrame/releases/download/v0.2.0/manifest.json"
)
# The tray reads its device labels from the shared registry: derive them here
# so renaming a target cannot silently rot this Windows-only suite.
PRIMARY_LABEL = MAINTAINED_TARGETS[0].label
PRIMARY_ADDRESS = "AA:BB:CC:DD:EE:01"
SECONDARY_LABEL = MAINTAINED_TARGETS[1].label


if sys.platform != "win32":
    class _WindowsOnlyTests(unittest.TestCase):
        @unittest.skip("the tray application is Windows-only")
        def test_windows_only(self) -> None:
            return None


    def load_tests(loader, _tests, _pattern):
        return loader.loadTestsFromTestCase(_WindowsOnlyTests)


class _RecordingPrompt:
    def __init__(self) -> None:
        self.calls: list[str] = []

    def call_soon(self, function: object, *_args: object) -> None:
        self.calls.append(getattr(function, "__name__", repr(function)))
        function()  # type: ignore[operator]

    def close_dialogs(self) -> None:
        return None

    async def confirm_forget(self, device_name: str) -> bool:
        self.calls.append(f"forget:{device_name}")
        return True


class _RecordingRoot:
    def withdraw(self) -> None:
        return None

    def quit(self) -> None:
        return None


class _RecordingShell:
    def __init__(self, **callbacks: object) -> None:
        self.callbacks = callbacks
        self.bridge_update_actions: list[tuple[bool, str]] = []
        self.notifications: list[tuple[str, str]] = []

    def set_autostart_checked(self, _checked: bool) -> None:
        return None

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
        return None

    def stop(self) -> None:
        return None


class _RecordingMonitor:
    def __init__(self) -> None:
        self.source: object | None = None
        self.current: SemVer | None = None
        self.presenter: object | None = None
        self.manual_checks: list[bool] = []

    async def run(self) -> None:
        return None

    async def check(self, *, manual: bool = False) -> None:
        self.manual_checks.append(manual)


class _ProductSource:
    def __init__(self, release: ProductRelease) -> None:
        self.release = release
        self.currents: list[SemVer] = []

    async def latest(self, current: SemVer) -> ProductRelease:
        self.currents.append(current)
        return self.release


class _RecordingController:
    def __init__(self) -> None:
        self.stopped = False
        self.firmware_actions: list[tuple[str, bool, str]] = []
        self.notifications: list[tuple[str, str]] = []

    def stop(self) -> None:
        self.stopped = True

    def set_firmware_action(self, label: str, enabled: bool, detail: str) -> None:
        self.firmware_actions.append((label, enabled, detail))

    def notify(self, title: str, body: str) -> None:
        self.notifications.append((title, body))


class TrayApplicationQuitTests(unittest.TestCase):
    def test_quit_closes_dialogs_before_leaving_the_main_loop(self) -> None:
        """root.quit cannot break out of a dialog's nested wait_window loop."""

        application = app.TrayApplication.__new__(app.TrayApplication)
        application._shutdown_lock = threading.Lock()
        application._firmware_updates = 0
        application._stopping = False
        application._loop = None
        application._root = _RecordingRoot()  # type: ignore[assignment]
        application._prompt = _RecordingPrompt()  # type: ignore[assignment]
        application._controller = _RecordingController()  # type: ignore[assignment]

        application._quit()
        application._quit()

        self.assertTrue(application._stopping)
        self.assertTrue(application._controller.stopped)
        self.assertEqual(application._prompt.calls, ["close_dialogs", "quit"])


class AutomaticQuitOtaTests(unittest.IsolatedAsyncioTestCase):
    def make_application(self):
        application = app.TrayApplication.__new__(app.TrayApplication)
        application._shutdown_lock = threading.Lock()
        application._firmware_updates = 0
        application._stopping = False
        application._loop = None
        application._root = _RecordingRoot()
        application._prompt = _RecordingPrompt()
        application._controller = _RecordingController()
        return application

    async def test_automatic_exit_waits_for_all_updates_and_failure_cleanup(self):
        application = self.make_application()
        started = [asyncio.Event(), asyncio.Event()]
        finish = [asyncio.Event(), asyncio.Event()]

        async def update(address):
            index = int(address)
            started[index].set()
            await finish[index].wait()
            if index == 1:
                raise RuntimeError("transfer failed")

        devices = {str(i): object() for i in range(2)}
        application._graph = SimpleNamespace(get=devices.get)
        application._run_firmware_update = update
        tasks = [asyncio.create_task(application._firmware_update_async(str(i), expected=devices[str(i)])) for i in range(2)]
        try:
            await asyncio.gather(*(event.wait() for event in started))
            self.assertFalse(application._can_auto_quit())
            application._auto_quit()
            self.assertFalse(application._stopping)
            finish[0].set()
            await tasks[0]
            self.assertFalse(application._can_auto_quit())
            finish[1].set()
            with self.assertRaisesRegex(RuntimeError, "transfer failed"):
                await tasks[1]
            self.assertTrue(application._can_auto_quit())
            application._auto_quit()
            self.assertTrue(application._stopping)
            application._run_firmware_update = AsyncMock()
            await application._firmware_update_async("0", expected=application._graph.get("0"))
            application._run_firmware_update.assert_not_awaited()
        finally:
            for task in tasks:
                task.cancel()
            await asyncio.gather(*tasks, return_exceptions=True)

    async def test_manual_exit_remains_available_during_ota(self):
        application = self.make_application()
        started = asyncio.Event()

        async def update(_address):
            started.set()
            await asyncio.Event().wait()

        devices = {str(i): object() for i in range(2)}
        application._graph = SimpleNamespace(get=devices.get)
        application._run_firmware_update = update
        task = asyncio.create_task(application._firmware_update_async("0", expected=application._graph.get("0")))
        try:
            await started.wait()
            self.assertFalse(application._can_auto_quit())
            application._auto_quit()
            self.assertFalse(application._stopping)
            application._quit()
            self.assertTrue(application._stopping)
            self.assertEqual(application._prompt.calls, ["close_dialogs", "quit"])
        finally:
            task.cancel()
            with self.assertRaises(asyncio.CancelledError):
                await task
        self.assertTrue(application._can_auto_quit())


class TrayEntryPointTests(unittest.TestCase):
    def test_version_exits_before_logging_or_ui_construction(self) -> None:
        output = io.StringIO()
        with (
            patch.object(app, "configure_logging", side_effect=AssertionError),
            patch.object(app, "TrayApplication", side_effect=AssertionError),
            redirect_stdout(output),
        ):
            code = app.main(["--version"])

        self.assertEqual(code, 0)
        self.assertEqual(
            output.getvalue(),
            f"QuotaFrame Bridge {__version__}\n",
        )


class TrayApplicationUpdateWiringTests(unittest.IsolatedAsyncioTestCase):
    def make_application(
        self,
        *,
        source: object,
        monitor: _RecordingMonitor | None = None,
        browser=lambda _url: True,
    ) -> tuple[app.TrayApplication, _RecordingShell, _RecordingMonitor]:
        selected_monitor = _RecordingMonitor() if monitor is None else monitor
        shell = _RecordingShell()

        def monitor_factory(actual_source, current, presenter, **options):
            selected_monitor.source = actual_source
            selected_monitor.current = current
            selected_monitor.presenter = presenter
            selected_monitor.windows_installed = options["windows_installed"]
            return selected_monitor

        def make_shell(**callbacks: object) -> _RecordingShell:
            shell.callbacks = callbacks
            return shell

        with (
            patch.object(app.tk, "Tk", return_value=_RecordingRoot()),
            patch.object(app, "TkPinPrompt", return_value=_RecordingPrompt()),
            patch.object(app, "PystrayShell", side_effect=make_shell),
            patch.object(app, "is_enabled", return_value=False),
        ):
            application = app.TrayApplication(
                release_source=source,
                monitor_factory=monitor_factory,
                browser=browser,
            )
        return application, shell, selected_monitor

    def test_snapshot_projects_live_device_status_before_the_summary(self) -> None:
        application = app.TrayApplication.__new__(app.TrayApplication)
        events: list[tuple[str, object]] = []
        snapshot = SimpleNamespace(
            devices=(TrayDeviceStatus("M5", True, True),)
        )

        class Shell:
            def set_device_statuses(self, devices: object) -> None:
                events.append(("devices", devices))

        class Controller:
            def apply(self, applied_snapshot: object, now: float) -> None:
                events.append(("summary", (applied_snapshot, now)))

        application._shell = Shell()  # type: ignore[assignment]
        application._controller = Controller()  # type: ignore[assignment]

        application._apply_snapshot(snapshot, 12.5)

        self.assertEqual(
            events,
            [
                ("devices", snapshot.devices),
                ("summary", (snapshot, 12.5)),
            ],
        )

    async def test_running_copy_type_reaches_the_update_monitor(self) -> None:
        for installed in (True, False, None):
            with self.subTest(installed=installed), patch.object(app, "is_installed", return_value=installed):
                _, _, monitor = self.make_application(source=object())
                self.assertIs(monitor.windows_installed, installed)

    async def test_download_menu_opens_the_validated_target_only_on_click(self) -> None:
        opened = []
        application, _, monitor = self.make_application(source=object(), browser=lambda url: opened.append(url) or True)
        for url in (PAGE_URL, PAGE_URL + "/portable.exe", PAGE_URL + "/setup.exe"):
            monitor.presenter.set_update_state(UpdateState(True, "Download", url))
            self.assertEqual(opened, [])
            application._bridge_update()
            self.assertEqual(opened, [url])
            opened.clear()

    async def test_menu_click_schedules_manual_check_on_worker_loop(self) -> None:
        source = _ProductSource(
            ProductRelease(
                SemVer.parse("0.2.0"), "v0.2.0", PAGE_URL, MANIFEST_URL
            )
        )
        application, shell, monitor = self.make_application(source=source)
        application._loop = asyncio.get_running_loop()

        callback = shell.callbacks["on_bridge_update"]
        callback()  # type: ignore[operator]
        for _ in range(10):
            if monitor.manual_checks:
                break
            await asyncio.sleep(0)

        self.assertEqual(monitor.manual_checks, [True])

    async def test_add_device_menu_runs_a_new_bounded_scan(self) -> None:
        source = _ProductSource(
            ProductRelease(SemVer.parse("0.2.0"), "v0.2.0", PAGE_URL, MANIFEST_URL)
        )
        application, shell, _monitor = self.make_application(source=source)
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

    async def test_forget_keeps_the_address_selected_before_confirmation(self) -> None:
        source = _ProductSource(
            ProductRelease(SemVer.parse("0.2.0"), "v0.2.0", PAGE_URL, MANIFEST_URL)
        )
        application, _shell, _monitor = self.make_application(source=source)
        application._loop = asyncio.get_running_loop()
        device = SimpleNamespace(name="M5", address="AA:BB:CC:DD:EE:01")

        class Graph:
            forgotten: list[str] = []

            @staticmethod
            def get(address: str):
                return device if device.address == address else None

            async def forget(self, address: str, *, expected=None) -> bool:
                self.forgotten.append(address)
                return True

        class Prompt(_RecordingPrompt):
            async def confirm_forget(self, device_name: str) -> bool:
                device.name = "M5 路EE01"
                return await super().confirm_forget(device_name)

        graph = Graph()
        application._graph = graph  # type: ignore[assignment]
        application._prompt = Prompt()  # type: ignore[assignment]

        application._forget_device(PRIMARY_ADDRESS)
        for _ in range(10):
            if graph.forgotten:
                break
            await asyncio.sleep(0)

        self.assertEqual(graph.forgotten, ["AA:BB:CC:DD:EE:01"])

    async def test_repair_keeps_the_device_selected_before_confirmation(self) -> None:
        source = _ProductSource(
            ProductRelease(SemVer.parse("0.2.0"), "v0.2.0", PAGE_URL, MANIFEST_URL)
        )
        application, _shell, _monitor = self.make_application(source=source)
        application._loop = asyncio.get_running_loop()
        pairer = SimpleNamespace(request_repair=Mock())
        manager = SimpleNamespace(reset=AsyncMock())
        device = SimpleNamespace(
            name="M5", address="AA:BB:CC:DD:EE:01", pairer=pairer, manager=manager
        )

        from quotaframe_bridge.service.multi_device import DeviceSession
        device.session = DeviceSession(device.name, manager)

        class Graph:
            @staticmethod
            def get(address: str):
                return device if device.address == address else None

        class Prompt(_RecordingPrompt):
            async def confirm_repair(self, device_name: str) -> bool:
                device.name = "M5 路EE01"
                return True

        application._graph = Graph()  # type: ignore[assignment]
        application._prompt = Prompt()  # type: ignore[assignment]
        application._service = SimpleNamespace(request_collection=lambda: None)

        application._repair(PRIMARY_ADDRESS)
        for _ in range(10):
            if manager.reset.await_count:
                break
            await asyncio.sleep(0)

        pairer.request_repair.assert_called_once_with()
        manager.reset.assert_awaited_once_with()

    async def test_catalog_is_independent_when_bridge_is_already_current(self):
        source = _ProductSource(None)
        application, _, monitor = self.make_application(source=source)
        await monitor.source.latest(SemVer.parse("0.1.0"))
        with patch.dict(os.environ, {"QUOTAFRAME_RELEASE_REPO": "example/panel"}, clear=True):
            self.assertEqual(application._firmware_manifest_url(),
                "https://github.com/example/panel/releases/latest/download/manifest.json")

    async def test_explicit_firmware_manifest_overrides_discovery(self) -> None:
        release = ProductRelease(
            SemVer.parse("0.2.0"), "v0.2.0", PAGE_URL, MANIFEST_URL
        )
        source = _ProductSource(release)
        application, _shell, monitor = self.make_application(source=source)
        assert monitor.source is not None
        await monitor.source.latest(SemVer.parse("0.1.0"))  # type: ignore[attr-defined]
        override = (
            "https://github.com/example/recovery/releases/download/"
            "v9.9.9/manifest.json"
        )

        with patch.dict(
            os.environ,
            {"QUOTAFRAME_FIRMWARE_MANIFEST_URL": override},
            clear=True,
        ):
            self.assertEqual(application._firmware_manifest_url(), override)

        self.assertEqual(
            app.FIRMWARE_MANIFEST_ENV,
            "QUOTAFRAME_FIRMWARE_MANIFEST_URL",
        )

    async def test_background_tasks_start_both_update_monitors(self) -> None:
        application = app.TrayApplication.__new__(app.TrayApplication)
        started: set[str] = set()

        class Monitor:
            def __init__(self, name: str) -> None:
                self.name = name

            async def run(self) -> None:
                started.add(self.name)

        class Service:
            async def run(self) -> None:
                started.add("service")

        async def publish_status(_graph: object) -> None:
            started.add("status")

        async def adopt_devices(_graph: object) -> None:
            started.add("adoption")

        graph = object()
        application._update_monitor = Monitor("bridge")  # type: ignore[assignment]
        application._firmware_update_monitor = Monitor("firmware")  # type: ignore[attr-defined]
        application._publish_status = publish_status  # type: ignore[method-assign]
        application._adopt_devices = adopt_devices  # type: ignore[method-assign]

        await application._run_background_tasks(Service(), graph)  # type: ignore[arg-type]

        self.assertEqual(
            started,
            {"service", "status", "bridge", "firmware"},
        )


class FirmwareUpdateWorkflowTests(unittest.IsolatedAsyncioTestCase):
    def make_application(self):
        application = app.TrayApplication.__new__(app.TrayApplication)
        application._shutdown_lock = threading.Lock()
        application._firmware_updates = 0
        application._stopping = False
        application._prompt = _RecordingPrompt()
        application._controller = _RecordingController()
        device = SimpleNamespace(name=PRIMARY_LABEL, address=PRIMARY_ADDRESS, update_task=None,
            session=SimpleNamespace(exclusive_active=False),
            manager=SimpleNamespace(device_status=DeviceStatus("Panel", True, 1, "",
                frozenset({"usage.v1", "ota.folder.v1"}), "0.4.0", "m5sticks3",
                OtaStatus("idle", 0, 0, ""), firmware_project="quotaframe"), connected=True))
        application._graph = SimpleNamespace(devices=[device], get=lambda address: device)
        application._firmware_update_monitor = SimpleNamespace(
            presentation=lambda *args: FirmwareUpdatePresentation(True, "0.5.0"))
        return application

    async def test_cancelled_confirmation_restores_update_action_immediately(self):
        application = self.make_application()
        with patch.object(application, "_firmware_manifest_url", return_value=MANIFEST_URL), patch.object(
            firmware_actions, "install_firmware", new=AsyncMock(return_value=None)
        ):
            await application._firmware_update_async(PRIMARY_ADDRESS, expected=application._graph.get(PRIMARY_ADDRESS))
        self.assertEqual(application._controller.firmware_actions[-1],
                         (PRIMARY_ADDRESS, True, "0.5.0"))

    async def test_missing_catalog_configuration_stops_before_installation(self):
        application = self.make_application()
        with patch.dict(os.environ, {}, clear=True), patch.object(firmware_actions, "catalog_url", side_effect=firmware_actions.ReleaseConfigError()):
            await application._firmware_update_async(PRIMARY_ADDRESS, expected=application._graph.get(PRIMARY_ADDRESS))
        self.assertEqual(application._controller.notifications,
            [(tr("firmware_unavailable"), tr("firmware_not_configured"))])

    async def test_shared_installation_marshals_progress_and_result_by_address(self):
        application = self.make_application()
        async def install(graph, device, url, confirm, progress):
            self.assertEqual(url, MANIFEST_URL)
            self.assertEqual(confirm, application._confirm_firmware)
            progress((2, 4))
            return SimpleNamespace(version="0.5.0")
        with patch.dict(os.environ, {app.FIRMWARE_MANIFEST_ENV: MANIFEST_URL}), patch.object(firmware_actions, "install_firmware", side_effect=install):
            await application._firmware_update_async(PRIMARY_ADDRESS, expected=application._graph.get(PRIMARY_ADDRESS))
        self.assertIn((PRIMARY_ADDRESS, True, tr("firmware_cancel_progress", percent=50)), application._controller.firmware_actions)
        self.assertIn((tr("firmware_complete"), tr("firmware_complete_body", label=PRIMARY_LABEL, version="0.5.0")), application._controller.notifications)
        self.assertEqual(application._firmware_updates, 0)

    async def test_failed_installation_reports_error_and_releases_busy_state(self):
        application = self.make_application()
        with patch.dict(os.environ, {app.FIRMWARE_MANIFEST_ENV: MANIFEST_URL}), patch.object(firmware_actions, "install_firmware", side_effect=firmware_actions.UpgradeError("low power")):
            await application._firmware_update_async(PRIMARY_ADDRESS, expected=application._graph.get(PRIMARY_ADDRESS))
        self.assertEqual(application._controller.notifications[-1], (tr("firmware_failed"), f"{PRIMARY_LABEL}：low power"))
        self.assertEqual(application._firmware_updates, 0)

    def test_periodic_refresh_preserves_active_update_progress(self):
        application = self.make_application()
        device = application._graph.devices[0]
        device.update_task = object()
        application._set_firmware_action(PRIMARY_ADDRESS, False, "42%")
        before = list(application._controller.firmware_actions)
        with patch.object(application, "_firmware_manifest_url", return_value=MANIFEST_URL):
            application._publish_firmware_actions(application._graph)
        self.assertEqual(application._controller.firmware_actions, before)

    async def test_replaced_device_receives_no_completion_or_failure_notification(self):
        for failure in (None, TimeoutError(), firmware_actions.UpgradeError("failed"), ValueError()):
            with self.subTest(failure=failure):
                application = self.make_application()
                device = application._graph.devices[0]
                async def install(*args):
                    application._graph.get = lambda address: object()
                    if failure is not None:
                        raise failure
                    return SimpleNamespace(version="0.5.0")
                with patch.object(application, "_firmware_manifest_url", return_value=MANIFEST_URL), patch.object(
                    firmware_actions, "install_firmware", side_effect=install
                ):
                    await application._firmware_update_async(PRIMARY_ADDRESS, expected=device)
                self.assertEqual(len(application._controller.notifications), 1)
                self.assertEqual(application._controller.notifications[0][0],
                                 tr("firmware_preparing", label=PRIMARY_LABEL))
                self.assertEqual(len(application._controller.firmware_actions), 1)

    def test_availability_uses_current_project_and_session(self):
        application = self.make_application()
        calls = []
        def presentation(*args):
            calls.append(args)
            return FirmwareUpdatePresentation(True, "0.5.0")
        application._firmware_update_monitor = SimpleNamespace(presentation=presentation)
        with patch.dict(os.environ, {app.FIRMWARE_MANIFEST_ENV: MANIFEST_URL}):
            application._publish_firmware_actions(application._graph)
        self.assertEqual(calls[0][3], "quotaframe")
        self.assertIs(calls[0][4], application._graph.devices[0].session)
        self.assertEqual(application._controller.firmware_actions[-1], (PRIMARY_ADDRESS, True, "0.5.0"))


class DpiAwarenessTests(unittest.TestCase):
    def test_the_process_declares_itself_dpi_aware(self) -> None:
        """Windows bitmap-stretches an unaware process: menus and text blur."""

        awareness = ctypes.c_int(-1)
        ctypes.windll.shcore.GetProcessDpiAwareness(None, ctypes.byref(awareness))

        self.assertEqual(awareness.value, app.DPI_SYSTEM_AWARE)

    def test_enabling_awareness_twice_is_not_an_error(self) -> None:
        """Windows refuses the second call, and a tray app must not die on it."""

        app.enable_dpi_awareness()


class TrayApplicationEntryPointTests(unittest.TestCase):
    def test_log_path_uses_localappdata_bridge_directory(self) -> None:
        original = os.environ.get("LOCALAPPDATA")
        os.environ["LOCALAPPDATA"] = r"C:\\BridgeData"
        try:
            self.assertEqual(
                app.log_path(),
                Path(r"C:\\BridgeData") / "quotaframe" / "bridge.log",
            )
        finally:
            if original is None:
                del os.environ["LOCALAPPDATA"]
            else:
                os.environ["LOCALAPPDATA"] = original


if __name__ == "__main__":
    unittest.main()
