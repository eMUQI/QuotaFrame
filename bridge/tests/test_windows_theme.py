"""Application-mode selection without a Windows desktop or native dialogs."""

from __future__ import annotations

import sys
import unittest
from unittest.mock import MagicMock, patch

from quotaframe_bridge.ui.windows_theme import apps_use_dark_theme


class WindowsThemeTests(unittest.TestCase):
    def test_reads_app_mode_instead_of_taskbar_mode(self) -> None:
        registry = MagicMock(REG_DWORD=4)
        for value, expected in ((0, True), (1, False)):
            with self.subTest(value=value), patch.dict(sys.modules, winreg=registry):
                registry.QueryValueEx.return_value = (value, 4)
                self.assertEqual(apps_use_dark_theme(), expected)
                self.assertEqual(registry.QueryValueEx.call_args.args[1], "AppsUseLightTheme")

    def test_missing_or_invalid_preference_uses_light_mode(self) -> None:
        registry = MagicMock(REG_DWORD=4)
        with patch.dict(sys.modules, winreg=registry):
            registry.OpenKey.side_effect = OSError("not available")
            self.assertFalse(apps_use_dark_theme())
            registry.OpenKey.side_effect = None
            for value in (("0", 1), (2, 4)):
                registry.QueryValueEx.return_value = value
                self.assertFalse(apps_use_dark_theme())


class DialogThemeWiringTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        import importlib.util
        from pathlib import Path
        from types import SimpleNamespace
        from quotaframe_bridge.ui import windows_theme

        tkinter = SimpleNamespace(
            font=MagicMock(), Toplevel=MagicMock(), TclError=RuntimeError,
        )
        path = Path(windows_theme.__file__).with_name("tk_pin_prompt.py")
        spec = importlib.util.spec_from_file_location("dialog_theme_test_module", path)
        cls.dialogs = importlib.util.module_from_spec(spec)
        with patch.dict(sys.modules, tkinter=tkinter):
            spec.loader.exec_module(cls.dialogs)

    def test_each_new_window_rereads_application_mode(self) -> None:
        from quotaframe_bridge.ui.windows_theme import DARK, LIGHT

        prompt = self.dialogs.TkPinPrompt(MagicMock())
        with patch.object(self.dialogs, "apps_use_dark_theme", side_effect=(False, True)), patch.object(
            self.dialogs, "apply_title_bar_theme"
        ) as title_bar:
            for dark, colors in ((False, LIGHT), (True, DARK)):
                window = prompt._window("Test")
                self.assertIs(prompt._colors, colors)
                window.configure.assert_called_with(bg=colors.bg)
                title_bar.assert_called_with(window, dark=dark)

    def test_title_bar_passes_both_modes_and_retries_supported_attribute(self) -> None:
        native = MagicMock()
        native.dwmapi.DwmSetWindowAttribute.side_effect = (1, 0, 0)
        observed = []

        def apply(_handle, attribute, enabled, _size):
            observed.append((attribute, enabled._obj.value))
            return 1 if len(observed) == 1 else 0

        native.dwmapi.DwmSetWindowAttribute.side_effect = apply
        with patch.object(self.dialogs.ctypes, "windll", native, create=True):
            self.assertTrue(self.dialogs.apply_title_bar_theme(MagicMock(), dark=True))
            self.assertTrue(self.dialogs.apply_title_bar_theme(MagicMock(), dark=False))
        self.assertEqual(observed, [(20, 1), (19, 1), (20, 0)])
