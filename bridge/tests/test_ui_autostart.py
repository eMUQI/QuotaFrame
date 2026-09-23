from __future__ import annotations

import sys
import unittest
from pathlib import Path

if sys.platform != "win32":
    raise unittest.SkipTest("autostart is a Windows registry Run entry")

import winreg

from quotaframe_bridge.ui.autostart import disable, enable, is_enabled

TEST_KEY = r"Software\quotaframe-bridge-tests\Run"


class AutostartTests(unittest.TestCase):
    def setUp(self) -> None:
        self.executable = Path(r"C:\tools\quotaframe-bridge.exe")
        self.addCleanup(self._remove_test_key)
        self._remove_test_key()

    def _remove_test_key(self) -> None:
        try:
            winreg.DeleteKey(winreg.HKEY_CURRENT_USER, TEST_KEY)
        except OSError:
            pass

    def test_missing_key_reads_as_disabled(self) -> None:
        self.assertFalse(
            is_enabled(executable=self.executable, key_path=TEST_KEY)
        )

    def test_enable_then_read_back(self) -> None:
        enable(executable=self.executable, key_path=TEST_KEY)
        self.assertTrue(
            is_enabled(executable=self.executable, key_path=TEST_KEY)
        )

    def test_enable_is_idempotent(self) -> None:
        enable(executable=self.executable, key_path=TEST_KEY)
        enable(executable=self.executable, key_path=TEST_KEY)
        self.assertTrue(
            is_enabled(executable=self.executable, key_path=TEST_KEY)
        )

    def test_disable_removes_the_value(self) -> None:
        enable(executable=self.executable, key_path=TEST_KEY)
        disable(key_path=TEST_KEY)
        self.assertFalse(
            is_enabled(executable=self.executable, key_path=TEST_KEY)
        )

    def test_disable_on_missing_value_is_silent(self) -> None:
        disable(key_path=TEST_KEY)

    def test_moved_executable_reads_as_disabled(self) -> None:
        enable(executable=self.executable, key_path=TEST_KEY)
        moved = Path(r"D:\elsewhere\quotaframe-bridge.exe")
        self.assertFalse(is_enabled(executable=moved, key_path=TEST_KEY))

    def test_stored_command_quotes_the_path_and_passes_the_flag(self) -> None:
        enable(executable=self.executable, key_path=TEST_KEY)
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, TEST_KEY) as key:
            stored, _kind = winreg.QueryValueEx(key, "QuotaFrameBridge")
        self.assertEqual(
            stored,
            '"C:\\tools\\quotaframe-bridge.exe" --autostarted',
        )


if __name__ == "__main__":
    unittest.main()
