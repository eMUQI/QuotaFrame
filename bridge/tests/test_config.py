from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from quotaframe_bridge.config import (
    config_path,
    load_config,
    read_codexbar_cli,
    read_log_level,
)

WINDOWS = "win32"
MACOS = "darwin"


class ConfigTests(unittest.TestCase):
    """Both platform layouts are exercised from either host.

    The platform is an explicit argument rather than the host's own, so a
    Windows developer still covers the macOS branch and the other way round.
    """

    def _environ(self, root: str, platform: str) -> dict[str, str]:
        return {"APPDATA": root} if platform == WINDOWS else {"HOME": root}

    def _directory(self, root: str, platform: str) -> Path:
        base = Path(root)
        if platform == MACOS:
            base = base / "Library" / "Application Support"
        return base / "quotaframe"

    def _write(self, root: str, platform: str, body: str) -> None:
        directory = self._directory(root, platform)
        directory.mkdir(parents=True, exist_ok=True)
        (directory / "config.toml").write_text(body, encoding="utf-8")

    def test_missing_file_yields_empty_mapping(self) -> None:
        for platform in (WINDOWS, MACOS):
            with self.subTest(platform=platform), tempfile.TemporaryDirectory() as root:
                self.assertEqual(
                    load_config(self._environ(root, platform), platform=platform),
                    {},
                )

    def test_malformed_file_yields_empty_mapping(self) -> None:
        for platform in (WINDOWS, MACOS):
            with self.subTest(platform=platform), tempfile.TemporaryDirectory() as root:
                self._write(root, platform, "this is not = valid = toml")
                self.assertEqual(
                    load_config(self._environ(root, platform), platform=platform),
                    {},
                )

    def test_absent_root_variable_yields_empty_mapping(self) -> None:
        for platform in (WINDOWS, MACOS):
            with self.subTest(platform=platform):
                self.assertIsNone(config_path({}, platform=platform))
                self.assertEqual(load_config({}, platform=platform), {})

    def test_macos_config_lives_in_application_support(self) -> None:
        path = config_path({"HOME": "/Users/panel"}, platform=MACOS)

        self.assertEqual(
            path,
            Path("/Users/panel/Library/Application Support/quotaframe/config.toml"),
        )

    def test_reads_both_keys(self) -> None:
        for platform in (WINDOWS, MACOS):
            with self.subTest(platform=platform), tempfile.TemporaryDirectory() as root:
                cli = Path(root) / "tools" / "codexbar-cli"
                self._write(
                    root,
                    platform,
                    f'codexbar_cli = "{cli.as_posix()}"\nlog_level = "DEBUG"\n',
                )
                environ = self._environ(root, platform)

                self.assertEqual(read_log_level(environ, platform=platform), "DEBUG")
                candidate = read_codexbar_cli(environ, platform=platform)
                assert candidate is not None
                self.assertEqual(candidate.name, "codexbar-cli")

    def test_log_level_defaults_to_info_when_absent(self) -> None:
        with tempfile.TemporaryDirectory() as root:
            self._write(root, WINDOWS, 'codexbar_cli = "x.exe"\n')
            self.assertEqual(
                read_log_level(self._environ(root, WINDOWS), platform=WINDOWS),
                "INFO",
            )

    def test_invalid_log_level_falls_back_to_info(self) -> None:
        with tempfile.TemporaryDirectory() as root:
            self._write(root, WINDOWS, 'log_level = "LOUD"\n')
            self.assertEqual(
                read_log_level(self._environ(root, WINDOWS), platform=WINDOWS),
                "INFO",
            )

    def test_blank_codexbar_cli_is_ignored(self) -> None:
        with tempfile.TemporaryDirectory() as root:
            self._write(root, WINDOWS, 'codexbar_cli = "   "\n')
            self.assertIsNone(
                read_codexbar_cli(self._environ(root, WINDOWS), platform=WINDOWS)
            )


if __name__ == "__main__":
    unittest.main()
