"""The macOS login item, written into a temporary HOME.

Unlike the Windows registry version these tests touch nothing outside the
temporary directory, so they run on any platform.
"""

from __future__ import annotations

import plistlib
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from quotaframe_bridge.ui.macos import autostart


class MacAutostartTests(unittest.TestCase):
    def setUp(self) -> None:
        self._home = tempfile.TemporaryDirectory()
        self.addCleanup(self._home.cleanup)
        self.home = Path(self._home.name)
        self.environ = {"HOME": str(self.home)}
        self.executable = self.home / "bin" / "python3"

    def _path(self) -> Path:
        return autostart.agent_path(self.environ)

    def test_disabled_by_default(self) -> None:
        self.assertFalse(
            autostart.is_enabled(executable=self.executable, environ=self.environ)
        )

    def test_enable_writes_a_launch_agent_that_runs_at_load(self) -> None:
        autostart.enable(executable=self.executable, environ=self.environ)

        document = plistlib.loads(self._path().read_bytes())
        self.assertEqual(document["Label"], autostart.LABEL)
        self.assertTrue(document["RunAtLoad"])
        self.assertIn(str(self.executable), document["ProgramArguments"])
        self.assertIn(autostart.AUTOSTART_FLAG, document["ProgramArguments"])

    def test_keep_alive_is_off_so_quit_actually_quits(self) -> None:
        """With KeepAlive on, launchd would relaunch us after every Quit."""

        autostart.enable(executable=self.executable, environ=self.environ)

        document = plistlib.loads(self._path().read_bytes())
        self.assertFalse(document["KeepAlive"])

    def test_enable_is_idempotent(self) -> None:
        autostart.enable(executable=self.executable, environ=self.environ)
        first = self._path().read_bytes()
        autostart.enable(executable=self.executable, environ=self.environ)

        self.assertEqual(self._path().read_bytes(), first)

    def test_round_trip(self) -> None:
        autostart.enable(executable=self.executable, environ=self.environ)
        self.assertTrue(
            autostart.is_enabled(executable=self.executable, environ=self.environ)
        )

        autostart.disable(environ=self.environ)

        self.assertFalse(
            autostart.is_enabled(executable=self.executable, environ=self.environ)
        )
        self.assertFalse(self._path().exists())

    def test_agent_for_a_different_interpreter_counts_as_disabled(self) -> None:
        """A stale agent would launch the wrong Python."""

        autostart.enable(executable=self.home / "old" / "python3", environ=self.environ)

        self.assertFalse(
            autostart.is_enabled(executable=self.executable, environ=self.environ)
        )

    def test_unreadable_agent_counts_as_disabled(self) -> None:
        path = self._path()
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("not a plist", encoding="utf-8")

        self.assertFalse(
            autostart.is_enabled(executable=self.executable, environ=self.environ)
        )

    def test_disable_tolerates_a_missing_agent(self) -> None:
        autostart.disable(environ=self.environ)

    def test_agent_lands_in_the_user_launch_agents_directory(self) -> None:
        self.assertEqual(
            self._path(),
            self.home / "Library" / "LaunchAgents" / f"{autostart.LABEL}.plist",
        )

    def test_source_launches_the_module_through_the_interpreter(self) -> None:
        with patch.object(sys, "frozen", False, create=True):
            autostart.enable(executable=self.executable, environ=self.environ)

        document = plistlib.loads(self._path().read_bytes())
        self.assertEqual(
            document["ProgramArguments"],
            [
                str(self.executable),
                "-m",
                "quotaframe_bridge.ui.macos.app",
                autostart.AUTOSTART_FLAG,
            ],
        )

    def test_frozen_app_launches_its_executable_directly(self) -> None:
        with patch.object(sys, "frozen", True, create=True):
            autostart.enable(executable=self.executable, environ=self.environ)

        document = plistlib.loads(self._path().read_bytes())
        self.assertEqual(
            document["ProgramArguments"],
            [str(self.executable), autostart.AUTOSTART_FLAG],
        )


if __name__ == "__main__":
    unittest.main()
