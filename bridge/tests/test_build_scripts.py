from __future__ import annotations

import os
import subprocess
import sys
import tempfile
import textwrap
import tomllib
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
WINDOWS_SCRIPT = ROOT / "bridge" / "build_exe.ps1"
MACOS_SCRIPT = ROOT / "bridge" / "build_app.sh"
DMG_SCRIPT = ROOT / "bridge" / "build_dmg.sh"
WINDOWS_CONSTRAINTS = ROOT / "bridge" / "release-constraints-windows.txt"
MACOS_CONSTRAINTS = ROOT / "bridge" / "release-constraints-macos.txt"
WINDOWS_LOCK = ROOT / "bridge" / "release-lock-windows.txt"
MACOS_LOCK = ROOT / "bridge" / "release-lock-macos.txt"
RELEASE_PYTHON = "3.13.14"


def _versions(path: Path) -> dict[str, str]:
    result: dict[str, str] = {}
    for raw_line in path.read_text("utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        requirement = line.split()[0]
        name, version = requirement.split("==", 1)
        result[name.lower()] = version
    return result


def _assert_hash_lock(test: unittest.TestCase, path: Path) -> None:
    for raw_line in path.read_text("utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        test.assertRegex(
            line,
            r"^[A-Za-z0-9_.-]+==[^ ]+ --hash=sha256:[0-9a-f]{64}$",
        )


class NativeReleaseInputTests(unittest.TestCase):
    def assert_release_lock_matches_constraints(
        self, constraints: Path, lock: Path
    ) -> None:
        _assert_hash_lock(self, lock)
        versions = _versions(lock)
        self.assertEqual(_versions(constraints), versions)
        self.assertIn("pip", versions)
        self.assertIn("setuptools", versions)
        self.assertIn("pyinstaller", versions)

    def assert_release_script_enforces_lock(
        self, script_path: Path, lock_name: str, python_version: str
    ) -> None:
        script = script_path.read_text("utf-8")
        self.assertIn(lock_name, script)
        self.assertIn("--require-hashes", script)
        self.assertIn("--only-binary=:all:", script)
        self.assertIn("--no-deps", script)
        self.assertIn("--no-build-isolation", script)
        self.assertIn("pip check", script)
        self.assertIn(python_version, script)

    def test_windows_release_inputs_are_locked(self) -> None:
        self.assert_release_lock_matches_constraints(WINDOWS_CONSTRAINTS, WINDOWS_LOCK)
        self.assert_release_script_enforces_lock(
            WINDOWS_SCRIPT, "release-lock-windows.txt", RELEASE_PYTHON
        )

    def test_macos_release_inputs_are_locked(self) -> None:
        self.assert_release_lock_matches_constraints(MACOS_CONSTRAINTS, MACOS_LOCK)
        self.assert_release_script_enforces_lock(
            MACOS_SCRIPT, "release-lock-macos.txt", RELEASE_PYTHON
        )

    def test_macos_runtime_declares_the_notification_framework(self) -> None:
        project = tomllib.loads((ROOT / "bridge" / "pyproject.toml").read_text("utf-8"))

        self.assertIn(
            "pyobjc-framework-UserNotifications>=10.3; sys_platform == 'darwin'",
            project["project"]["dependencies"],
        )


@unittest.skipIf(
    sys.platform == "win32",
    "build_dmg.sh is a POSIX shell script and cannot run on Windows",
)
class DmgPackagingTests(unittest.TestCase):
    def test_builds_requested_dmg_and_removes_staging_directory(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            app = root / "Fixture App.app"
            app.mkdir()
            output = root / "output with spaces" / "fixture.dmg"
            capture = root / "staging-path.txt"
            tools = root / "tools"
            tools.mkdir()
            hdiutil = tools / "hdiutil"
            hdiutil.write_text(
                textwrap.dedent(
                    """\
                    #!/usr/bin/env bash
                    set -euo pipefail
                    source=""
                    destination=""
                    while (($#)); do
                        case "$1" in
                            -srcfolder)
                                source="$2"
                                shift 2
                                ;;
                            *)
                                destination="$1"
                                shift
                                ;;
                        esac
                    done
                    test -d "$source/Fixture App.app"
                    test -L "$source/Applications"
                    test "$(readlink "$source/Applications")" = /Applications
                    mkdir -p "$(dirname "$destination")"
                    printf 'fake dmg\n' >"$destination"
                    printf '%s\n' "$source" >"$CAPTURE_PATH"
                    """
                ),
                encoding="utf-8",
            )
            hdiutil.chmod(0o755)
            environment = {
                **os.environ,
                "CAPTURE_PATH": str(capture),
                "PATH": f"{tools}{os.pathsep}{os.environ['PATH']}",
            }

            completed = subprocess.run(
                (str(DMG_SCRIPT), "--app", str(app), "--output", str(output)),
                capture_output=True,
                text=True,
                timeout=30,
                check=False,
                env=environment,
            )

            self.assertEqual(completed.returncode, 0, completed.stderr)
            self.assertEqual(output.read_bytes(), b"fake dmg\n")
            staging = Path(capture.read_text("utf-8").strip())
            self.assertFalse(staging.exists())


if __name__ == "__main__":
    unittest.main()
