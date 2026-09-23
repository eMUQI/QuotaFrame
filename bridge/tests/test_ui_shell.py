from __future__ import annotations

import unittest

from quotaframe_bridge.ui.status import TrayState
from ui_fakes import FakePinPrompt, FakeTrayShell


class FakeTrayShellTests(unittest.TestCase):
    def test_records_the_complete_user_visible_update(self) -> None:
        """Fails if a fake stops preserving a controller-visible tray effect."""

        tray = FakeTrayShell()

        tray.set_state(TrayState.WARN)
        tray.set_tooltip("AI usage panel · M5 disconnected")
        tray.notify("AI usage panel", "M5 disconnected")
        tray.set_autostart_checked(True)
        tray.set_info_lines(("M5 disconnected", "Codex 42% · Claude 17%"))
        tray.set_firmware_action("M5", True, "可更新")
        tray.set_bridge_update_action(True, "下载 v0.2.0…")
        tray.stop()

        self.assertEqual(tray.states, [TrayState.WARN])
        self.assertEqual(tray.tooltips, ["AI usage panel · M5 disconnected"])
        self.assertEqual(tray.notifications, [("AI usage panel", "M5 disconnected")])
        self.assertTrue(tray.autostart_checked)
        self.assertEqual(
            tray.info_lines,
            [("M5 disconnected", "Codex 42% · Claude 17%")],
        )
        self.assertTrue(tray.stopped)
        self.assertEqual(tray.firmware_actions, [("M5", True, "可更新")])
        self.assertEqual(tray.bridge_update_actions, [(True, "下载 v0.2.0…")])


class FakePinPromptTests(unittest.IsolatedAsyncioTestCase):
    async def test_returns_scripted_answers_and_records_the_device(self) -> None:
        """Fails if pairing tests cannot observe or control prompt answers."""

        prompt = FakePinPrompt(pin="654321", confirm=False)

        self.assertEqual(await prompt.ask_pin("M5StickS3"), "654321")
        self.assertFalse(await prompt.confirm_repair("M5StickS3"))
        self.assertFalse(await prompt.confirm_forget("M5StickS3"))
        self.assertEqual(prompt.asked, ["M5StickS3"])
        self.assertEqual(prompt.confirmed, ["M5StickS3"])
        self.assertEqual(prompt.forgotten, ["M5StickS3"])


if __name__ == "__main__":
    unittest.main()
