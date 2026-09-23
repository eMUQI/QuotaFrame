"""Language selection and copy rendering without native UI or Bluetooth access."""

from __future__ import annotations

import os
import string
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import Mock, patch

from quotaframe_bridge import i18n
from quotaframe_bridge.catalog import MESSAGES
from quotaframe_bridge.config import config_path, read_language


class LanguageTests(unittest.TestCase):
    def test_config_validates_language_and_preserves_other_settings(self) -> None:
        for platform in ("win32", "darwin"):
            with tempfile.TemporaryDirectory() as root:
                environ = {"APPDATA": root, "HOME": root}
                path = config_path(environ, platform=platform)
                path.parent.mkdir(parents=True)
                for setting, expected in (
                    ("", "auto"), ("'auto'", "auto"), ("'zh'", "zh"),
                    ("' EN '", "en"), ("'fr'", "auto"), ("42", "auto"),
                    ("[]", "auto"), ("'unterminated", "auto"),
                ):
                    with self.subTest(platform=platform, setting=setting):
                        body = "log_level = 'DEBUG'\n"
                        if setting:
                            body += f"language = {setting}\n"
                        path.write_text(body, encoding="utf-8")
                        self.assertEqual(read_language(environ, platform=platform), expected)
                        self.assertEqual(path.read_text(encoding="utf-8"), body)
                path.unlink()
                self.assertEqual(read_language(environ, platform=platform), "auto")

    def test_explicit_language_bypasses_platform_detection(self) -> None:
        for configured in ("zh", "en"):
            with patch.object(i18n, "read_language", return_value=configured), patch.object(
                i18n, "system_language", side_effect=AssertionError("unexpected detection")
            ):
                self.assertEqual(i18n.language.__wrapped__(), configured)
        with patch.object(i18n, "read_language", return_value="auto"), patch.object(
            i18n, "system_language", return_value="zh"
        ):
            self.assertEqual(i18n.language.__wrapped__(), "zh")

    def test_macos_uses_primary_ui_language_not_region(self) -> None:
        for preferred, expected in (
            (["zh-Hans-CN"], "zh"), (["zh_Hant_TW"], "zh"),
            (["zh-HK"], "zh"), (["en-CN", "zh-Hans"], "en"),
            (["fr-FR", "zh-Hans"], "en"), ([], "en"),
        ):
            foundation = SimpleNamespace(NSLocale=SimpleNamespace(
                preferredLanguages=lambda: preferred
            ))
            with self.subTest(preferred=preferred), patch.object(i18n.sys, "platform", "darwin"), patch.dict(
                sys.modules, {"Foundation": foundation}
            ), patch("locale.getlocale", side_effect=AssertionError("regional format used")):
                self.assertEqual(i18n.system_language(), expected)

    def test_windows_uses_ui_langid_including_chinese_variants(self) -> None:
        for langid, expected in ((0x0804, "zh"), (0x0404, "zh"), (0x0C04, "zh"), (0x1004, "zh"), (0x0409, "en"), (0, "en")):
            api = Mock(return_value=langid)
            windll = SimpleNamespace(kernel32=SimpleNamespace(GetUserDefaultUILanguage=api))
            with self.subTest(langid=langid), patch.object(i18n.sys, "platform", "win32"), patch.object(
                i18n.ctypes, "windll", windll, create=True
            ), patch("locale.getlocale", side_effect=AssertionError("regional format used")):
                self.assertEqual(i18n.system_language(), expected)
                api.assert_called_once_with()

    def test_detection_failure_and_unsupported_platform_fall_back_to_english(self) -> None:
        with patch.object(i18n.sys, "platform", "darwin"), patch.dict(sys.modules, {"Foundation": None}):
            self.assertEqual(i18n.system_language(), "en")
        api = Mock(side_effect=OSError("unavailable"))
        with patch.object(i18n.sys, "platform", "win32"), patch.object(
            i18n.ctypes, "windll", SimpleNamespace(kernel32=SimpleNamespace(GetUserDefaultUILanguage=api)), create=True
        ):
            self.assertEqual(i18n.system_language(), "en")
        with patch.object(i18n.sys, "platform", "linux"):
            self.assertEqual(i18n.system_language(), "en")

    def test_catalog_languages_have_matching_renderable_placeholders(self) -> None:
        formatter = string.Formatter()
        for key, (zh, en) in MESSAGES.items():
            with self.subTest(key=key):
                fields = lambda text: {field for _, field, _, _ in formatter.parse(text) if field}
                self.assertEqual(fields(zh), fields(en))
                self.assertTrue(zh and en)
                self.assertFalse(any("\u4e00" <= char <= "\u9fff" for char in en))
                for selected in ("zh", "en"):
                    with patch.object(i18n, "language", return_value=selected):
                        rendered = i18n.tr(key, **dict.fromkeys(fields(zh), "42"))
                        self.assertNotIn("{", rendered)

    def test_fresh_process_renders_consistent_copy_and_keeps_startup_language(self) -> None:
        script = '''
import asyncio
from quotaframe_bridge.config import config_path
from quotaframe_bridge.i18n import language, tr
from quotaframe_bridge.ui.macos.menu_model import build_menu, MenuAction
from quotaframe_bridge.ui.macos.dialogs import MacDialogs, AlertBindings
from quotaframe_bridge.ui.notifications import NotificationPolicy, NotificationKind
from quotaframe_bridge.ui.status import DeviceStatus, format_device_summary, display_width, DEVICE_SUMMARY_WIDTH
from quotaframe_bridge.service.update import IDLE_STATE
selected = language()
expected = "Add device…" if selected == "en" else "添加设备…"
assert next(row for row in build_menu() if row.action is MenuAction.ADD_DEVICE).label == expected
assert IDLE_STATE.detail == tr("check_bridge_update")
assert build_menu()[0].label == tr("starting_devices")
notification, = NotificationPolicy().emit(NotificationKind.DEVICE_LOST, 100, device="Desk {A}")
assert notification.body == tr("panel_disconnected_body", device="Desk {A}")
alerts = []
dialogs = MacDialogs(AlertBindings(lambda *args: alerts.append(args) or 1, lambda url: None))
asyncio.run(dialogs.confirm_repair("Desk"))
assert alerts[0][1] == tr("repair_macos_body")
for devices in ((DeviceStatus("X" * 80, False, True),), tuple(DeviceStatus(str(i), i < 18, True) for i in range(20))):
    assert display_width(format_device_summary(devices)) <= DEVICE_SUMMARY_WIDTH
config_path().write_text("language = '" + ("zh" if selected == "en" else "en") + "'", encoding="utf-8")
assert language() == selected
assert tr("add_device") == expected
'''
        for selected in ("zh", "en"):
            with self.subTest(language=selected), tempfile.TemporaryDirectory() as root:
                environ = dict(os.environ, HOME=root, APPDATA=root)
                path = config_path(environ)
                path.parent.mkdir(parents=True)
                path.write_text(f"language = '{selected}'", encoding="utf-8")
                result = subprocess.run([sys.executable, "-c", script], env=environ, capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)
