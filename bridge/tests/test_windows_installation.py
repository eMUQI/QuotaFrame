from __future__ import annotations

import sys
import unittest
from unittest.mock import patch

if sys.platform != "win32":
    raise unittest.SkipTest("Windows installation detection")

from quotaframe_bridge.ui import windows_installation as installation


class WindowsInstallationTests(unittest.TestCase):
    def test_only_the_registered_executable_is_installed(self):
        for executable, expected in (
            (r"C:\Custom\QuotaFrame\quotaframe-bridge.exe", True),
            (r"c:\custom\quotaframe\QUOTAFRAME-BRIDGE.EXE", True),
            (r"C:\Users\Tester\Downloads\quotaframe-bridge.exe", False),
            (r"C:\Custom\QuotaFrame\test-copy.exe", False),
        ):
            with self.subTest(executable=executable), patch.object(sys, "frozen", True, create=True), patch.object(
                sys, "executable", executable
            ), patch.object(installation.winreg, "OpenKey") as opened, patch.object(
                installation.winreg, "QueryValueEx", return_value=("C:/Custom/QuotaFrame/", installation.winreg.REG_SZ)
            ):
                self.assertIs(installation.is_installed(), expected)
                self.assertEqual(opened.call_args.args[-1], installation.winreg.KEY_READ | installation.winreg.KEY_WOW64_64KEY)

    def test_missing_registration_is_portable_but_unreadable_is_unknown(self):
        for error, expected in ((FileNotFoundError(), False), (PermissionError(), None)):
            with self.subTest(error=error), patch.object(sys, "frozen", True, create=True), patch.object(
                installation.winreg, "OpenKey", side_effect=error
            ):
                self.assertIs(installation.is_installed(), expected)

    def test_missing_install_location_is_unknown_and_closes_key(self):
        with patch.object(sys, "frozen", True, create=True), patch.object(
            installation.winreg, "OpenKey"
        ) as opened, patch.object(
            installation.winreg, "QueryValueEx", side_effect=FileNotFoundError()
        ):
            self.assertIsNone(installation.is_installed())
            opened.return_value.__exit__.assert_called_once()

    def test_source_run_and_invalid_registry_data_are_unknown(self):
        with patch.object(sys, "frozen", False, create=True), patch.object(installation.winreg, "OpenKey") as opened:
            self.assertIsNone(installation.is_installed())
            opened.assert_not_called()
        for location in ("", "relative", 42):
            with self.subTest(location=location), patch.object(sys, "frozen", True, create=True), patch.object(
                installation.winreg, "OpenKey"
            ), patch.object(installation.winreg, "QueryValueEx", return_value=(location, installation.winreg.REG_SZ)):
                self.assertIsNone(installation.is_installed())
