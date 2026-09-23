"""Every firmware Unity source is registered in exactly one test app.

Hosted CI compiles the test applications but never executes them, so a source
dropped from an `idf_component_register` SRCS list still produces a green
build with its cases silently gone. Only this check fails.
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path

from quotaframe_bridge.targets import TARGETS

REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
FIRMWARE = REPOSITORY_ROOT / "firmware"
_SRCS = re.compile(r'"([^"]+\.(?:cpp|c))"')


def test_app_directories() -> list[Path]:
    shared = sorted((FIRMWARE / "test_apps").iterdir())
    registered = [REPOSITORY_ROOT / path for target in TARGETS for path in target.test_apps]
    return [path for path in shared + registered if path.is_dir()]


class FirmwareTestAppTests(unittest.TestCase):
    def test_every_unity_source_is_registered_exactly_once(self) -> None:
        for app in test_app_directories():
            registered = set(_SRCS.findall((app / "main" / "CMakeLists.txt").read_text()))
            present = {path.name for path in (app / "main").glob("test_*.cpp")}
            with self.subTest(app=app.relative_to(REPOSITORY_ROOT)):
                self.assertEqual(present - registered, set(), "present but not built")
                unresolved = {
                    name
                    for name in registered
                    if not (app / "main" / name).resolve().is_file()
                }
                self.assertEqual(unresolved, set(), "listed but missing")

    def test_target_logic_tests_are_owned_by_their_target(self) -> None:
        """A target's board logic tests live under that target, not in `test_apps/`.

        `test_apps/` is reachable by every target, so a board's sources placed
        there are built once for the whole repository and read as shared.
        """

        for app in sorted((FIRMWARE / "test_apps").iterdir()):
            if not app.is_dir():
                continue
            sources = _SRCS.findall((app / "main" / "CMakeLists.txt").read_text())
            outside = [name for name in sources if "targets/" in name]
            with self.subTest(app=app.relative_to(REPOSITORY_ROOT)):
                self.assertEqual(outside, [], "shared app builds target sources")

    def test_every_app_runs_all_of_its_cases(self) -> None:
        """No test image filters its own cases by tag.

        `unity_run_tests_by_tag` matches with `strstr`, so "[ota]" does not
        reach "[ota_ui]" or "[ota_power]". A filtered runner builds and boots
        normally while silently skipping every case whose tag it misses.
        """

        for app in test_app_directories():
            runner = (app / "main" / "test_main.cpp").read_text()
            with self.subTest(app=app.relative_to(REPOSITORY_ROOT)):
                self.assertNotIn("unity_run_tests_by_tag", runner)
                self.assertIn("unity_run_all_tests", runner)

    def test_every_registered_target_test_app_exists(self) -> None:
        for target in TARGETS:
            for path in target.test_apps:
                app = REPOSITORY_ROOT / path
                with self.subTest(target=target.id, path=path):
                    self.assertTrue((app / "CMakeLists.txt").is_file())
                    self.assertTrue((app / "main" / "CMakeLists.txt").is_file())


if __name__ == "__main__":
    unittest.main()
