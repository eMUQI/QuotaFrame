from __future__ import annotations

import io
import unittest
from contextlib import redirect_stderr
from contextlib import redirect_stdout
from unittest.mock import patch

from quotaframe_bridge.i18n import tr
from quotaframe_bridge import __version__
from quotaframe_bridge.ui.macos import dialogs


class RecordingBindings:
    def __init__(self, choice: int = 0) -> None:
        self.alerts: list[tuple[str, str, tuple[str, ...]]] = []
        self.urls: list[str] = []
        self.choice = choice

    def run_alert(self, title: str, body: str, buttons: tuple[str, ...]) -> int:
        self.alerts.append((title, body, buttons))
        return self.choice

    def open_url(self, url: str) -> None:
        self.urls.append(url)

    def as_bindings(self) -> dialogs.AlertBindings:
        return dialogs.AlertBindings(run_alert=self.run_alert, open_url=self.open_url)


class RepairDialogTests(unittest.IsolatedAsyncioTestCase):
    async def test_repair_explains_the_manual_steps(self) -> None:
        recorder = RecordingBindings(choice=1)

        await dialogs.MacDialogs(recorder.as_bindings()).confirm_repair("M5StickS3")

        title, body, buttons = recorder.alerts[0]
        self.assertEqual(title, dialogs.REPAIR_TITLE)
        self.assertEqual(body, tr("repair_macos_body"))
        self.assertEqual(buttons, (dialogs.REPAIR_CONFIRM, dialogs.REPAIR_CANCEL))

    async def test_confirming_opens_bluetooth_settings(self) -> None:
        recorder = RecordingBindings(choice=0)

        await dialogs.MacDialogs(recorder.as_bindings()).confirm_repair("M5StickS3")

        self.assertEqual(recorder.urls, [dialogs.BLUETOOTH_SETTINGS_URL])

    async def test_cancelling_opens_nothing(self) -> None:
        recorder = RecordingBindings(choice=1)

        await dialogs.MacDialogs(recorder.as_bindings()).confirm_repair("M5StickS3")

        self.assertEqual(recorder.urls, [])

    async def test_repair_never_claims_to_have_repaired(self) -> None:
        """Nothing in this process can remove a macOS bond."""

        for choice in (0, 1):
            with self.subTest(choice=choice):
                recorder = RecordingBindings(choice=choice)

                result = await dialogs.MacDialogs(
                    recorder.as_bindings()
                ).confirm_repair("M5StickS3")

                self.assertFalse(result)

    async def test_ask_pin_refuses_rather_than_inventing_a_code(self) -> None:
        recorder = RecordingBindings()

        with self.assertRaises(NotImplementedError):
            await dialogs.MacDialogs(recorder.as_bindings()).ask_pin("M5StickS3")

        self.assertEqual(recorder.alerts, [])


class AlreadyRunningDialogTests(unittest.TestCase):
    def test_points_at_the_menu_bar_not_the_taskbar(self) -> None:
        recorder = RecordingBindings()

        dialogs.MacDialogs(recorder.as_bindings()).show_already_running()

        title, body, buttons = recorder.alerts[0]
        self.assertEqual(title, dialogs.ALREADY_RUNNING_TITLE)
        self.assertEqual(body, tr("already_running_macos"))
        self.assertEqual(buttons, (dialogs.ALREADY_RUNNING_CONFIRM,))


class OpenLogTests(unittest.TestCase):
    def test_uses_open_rather_than_a_shell(self) -> None:
        calls: list[list[str]] = []

        dialogs.open_log_directory(
            "/tmp/logs",
            runner=lambda argv, check: calls.append(argv),
        )

        self.assertEqual(calls, [["/usr/bin/open", "/tmp/logs"]])


class MenuBarEntryPointTests(unittest.TestCase):
    """The macOS module entry point rejects unsupported platforms cleanly."""

    def test_non_macos_hosts_get_a_sentence_not_a_traceback(self) -> None:
        from quotaframe_bridge.ui.macos.app import main

        error = io.StringIO()
        with redirect_stderr(error):
            code = main([], platform="win32")

        self.assertEqual(code, 1)
        self.assertIn("macOS", error.getvalue())
        self.assertIn("quotaframe-bridge.exe", error.getvalue())

    def test_version_exits_before_platform_or_permission_checks(self) -> None:
        from quotaframe_bridge.ui.macos import app

        output = io.StringIO()
        with (
            patch.object(app, "configure_logging", side_effect=AssertionError),
            patch.object(app, "preflight", side_effect=AssertionError),
            redirect_stdout(output),
        ):
            code = app.main(["--version"], platform="win32")

        self.assertEqual(code, 0)
        self.assertEqual(
            output.getvalue(),
            f"QuotaFrame Bridge {__version__}\n",
        )


if __name__ == "__main__":
    unittest.main()
