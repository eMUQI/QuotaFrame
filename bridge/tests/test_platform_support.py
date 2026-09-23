"""Platform layout and platform dispatch, exercised from any host."""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from quotaframe_bridge.cli.instance_lock import default_lock_path
from quotaframe_bridge.paths import (
    config_directory,
    data_directory,
    log_directory,
    tray_assets,
)
from quotaframe_bridge.sources.codexbar import WinCodexBarSource
from quotaframe_bridge.sources.codexbar_mac import MacCodexBarSource
from quotaframe_bridge.sources.selection import (
    UnsupportedPlatformError,
    create_usage_source,
)
from quotaframe_bridge.ui.macos import icons
from quotaframe_bridge.ui.status import TrayState

WINDOWS_ENV = {"APPDATA": r"C:\Users\panel\AppData\Roaming", "LOCALAPPDATA": r"C:\Users\panel\AppData\Local"}
MACOS_ENV = {"HOME": "/Users/panel"}


class PlatformPathTests(unittest.TestCase):
    def test_windows_keeps_the_roaming_and_local_split(self) -> None:
        self.assertEqual(
            config_directory(WINDOWS_ENV, platform="win32"),
            Path(r"C:\Users\panel\AppData\Roaming") / "quotaframe",
        )
        self.assertEqual(
            data_directory(WINDOWS_ENV, platform="win32"),
            Path(r"C:\Users\panel\AppData\Local") / "quotaframe",
        )
        self.assertEqual(
            log_directory(WINDOWS_ENV, platform="win32"),
            data_directory(WINDOWS_ENV, platform="win32"),
        )

    def test_macos_follows_the_apple_layout(self) -> None:
        self.assertEqual(
            config_directory(MACOS_ENV, platform="darwin"),
            Path("/Users/panel/Library/Application Support/quotaframe"),
        )
        self.assertEqual(
            data_directory(MACOS_ENV, platform="darwin"),
            Path("/Users/panel/Library/Application Support/quotaframe"),
        )
        self.assertEqual(
            log_directory(MACOS_ENV, platform="darwin"),
            Path("/Users/panel/Library/Logs/quotaframe"),
        )

    def test_macos_never_reads_the_windows_variables(self) -> None:
        self.assertIsNone(config_directory(WINDOWS_ENV, platform="darwin"))

    def test_data_directory_always_resolves_so_the_lock_has_a_home(self) -> None:
        for platform in ("win32", "darwin"):
            with self.subTest(platform=platform):
                self.assertTrue(data_directory({}, platform=platform).is_absolute())

    def test_lock_path_sits_inside_the_data_directory(self) -> None:
        for platform, environ in (("win32", WINDOWS_ENV), ("darwin", MACOS_ENV)):
            with self.subTest(platform=platform):
                path = default_lock_path(environ, platform=platform)
                self.assertEqual(path.name, "bridge.lock")
                self.assertEqual(
                    path.parent,
                    data_directory(environ, platform=platform),
                )


class SourceSelectionTests(unittest.TestCase):
    def test_windows_host_speaks_the_win_codexbar_dialect(self) -> None:
        source = create_usage_source(None, platform="win32")

        self.assertIsInstance(source, WinCodexBarSource)

    def test_macos_host_speaks_the_upstream_dialect(self) -> None:
        source = create_usage_source(None, platform="darwin")

        self.assertIsInstance(source, MacCodexBarSource)

    def test_unsupported_host_fails_with_a_named_platform(self) -> None:
        with self.assertRaises(UnsupportedPlatformError) as raised:
            create_usage_source(None, platform="linux")

        self.assertEqual(raised.exception.platform, "linux")


class TrayAssetTests(unittest.TestCase):
    def test_one_anchor_reaches_every_file_the_shells_load(self) -> None:
        """Both shells resolve artwork through `tray_assets`.

        That resolution counts a fixed number of parent directories, so
        moving `paths` inside the package breaks it silently: nothing fails
        until an icon is drawn.
        """

        directory = tray_assets()
        self.assertTrue(directory.is_dir())

        expected = {icons.NORMAL_FILENAME, icons.ATTENTION_FILENAME}
        # pystray_shell.ICON_SIZE. That module imports winreg, so it cannot be
        # read from a non-Windows host running this test.
        expected |= {
            f"tray-{state.value}-{theme}-32.png"
            for state in TrayState
            for theme in ("dark", "light")
        }
        for filename in sorted(expected):
            with self.subTest(filename=filename):
                self.assertTrue((directory / filename).is_file())


if __name__ == "__main__":
    unittest.main()
