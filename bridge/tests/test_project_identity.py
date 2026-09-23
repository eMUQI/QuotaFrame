from __future__ import annotations

import re
import tomllib
import unittest
from pathlib import Path

from quotaframe_bridge.paths import CONFIG_DIRECTORY, DATA_DIRECTORY
from quotaframe_bridge.targets import TARGETS
from quotaframe_bridge.ui.macos.autostart import LABEL


ROOT = Path(__file__).resolve().parents[2]
LOCK_FILES = tuple(ROOT / target.firmware_project / "dependencies.lock" for target in TARGETS)


def _top_level_dependencies(lock_file: Path) -> set[str]:
    dependencies: set[str] = set()
    in_dependencies = False
    for line in lock_file.read_text("utf-8").splitlines():
        if line == "dependencies:":
            in_dependencies = True
            continue
        if in_dependencies and line and not line.startswith(" "):
            break
        match = re.fullmatch(
            r"  ([A-Za-z0-9_.-]+(?:/[A-Za-z0-9_.-]+)?):",
            line,
        )
        if in_dependencies and match:
            dependencies.add(match.group(1))
    return dependencies


class ProjectIdentityTests(unittest.TestCase):
    def test_public_identifiers_use_one_product_root(self) -> None:
        project = tomllib.loads((ROOT / "bridge/pyproject.toml").read_text("utf-8"))
        self.assertEqual(project["project"]["name"], "quotaframe-bridge")
        self.assertEqual(
            project["project"]["scripts"],
            {"quotaframe-bridge": "quotaframe_bridge.cli.main:main"},
        )
        self.assertEqual(
            project["tool"]["setuptools"]["dynamic"]["version"]["attr"],
            "quotaframe_bridge.__version__",
        )
        self.assertEqual(CONFIG_DIRECTORY, "quotaframe")
        self.assertEqual(DATA_DIRECTORY, "quotaframe")
        self.assertEqual(LABEL, "com.quotaframe.bridge")

    def test_project_license_exists(self) -> None:
        license_text = (ROOT / "LICENSE").read_text("utf-8")
        self.assertIn("Mozilla Public License Version 2.0", license_text)

    def test_notices_cover_all_locked_production_dependencies(self) -> None:
        locked = set().union(*map(_top_level_dependencies, LOCK_FILES))
        notices = (ROOT / "THIRD_PARTY_LICENSES.md").read_text("utf-8")
        headings = set(re.findall(r"(?m)^## `([^`]+)`$", notices))
        self.assertLessEqual(locked, headings)

    def test_notices_cover_platform_runtime_dependencies(self) -> None:
        notices = (ROOT / "THIRD_PARTY_LICENSES.md").read_text("utf-8")
        headings = set(re.findall(r"(?m)^## `([^`]+)`$", notices))

        self.assertLessEqual(
            {
                "six",
                "pyobjc-core",
                "pyobjc-framework-Cocoa",
                "pyobjc-framework-CoreBluetooth",
                "pyobjc-framework-libdispatch",
                "pyobjc-framework-UserNotifications",
                "winrt-runtime",
                "winrt-windows-devices-bluetooth",
                "winrt-windows-devices-bluetooth-advertisement",
                "winrt-windows-devices-bluetooth-genericattributeprofile",
                "winrt-windows-devices-enumeration",
                "winrt-windows-devices-radios",
                "winrt-windows-foundation",
                "winrt-windows-foundation-collections",
                "winrt-windows-storage-streams",
            },
            headings,
        )

    def test_repository_python_tools_use_the_current_package_name(self) -> None:
        stale_import = re.compile(
            r"\b(?:from|import)\s+(?:m5_usage_bridge|esp_ai_dashboard_bridge)\b"
        )
        offenders = []
        for path in (ROOT / "tools").glob("*.py"):
            if stale_import.search(path.read_text("utf-8")):
                offenders.append(path.relative_to(ROOT).as_posix())
        self.assertEqual(offenders, [])


if __name__ == "__main__":
    unittest.main()
