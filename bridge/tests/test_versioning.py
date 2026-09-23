from __future__ import annotations

import subprocess
import sys
import tomllib
import unittest
from pathlib import Path

from quotaframe_bridge import __version__
from quotaframe_bridge.versioning import SemVer, VersionError


ROOT = Path(__file__).resolve().parents[2]


class SemVerTests(unittest.TestCase):
    def test_semver_precedence_and_channels(self) -> None:
        ordered = [
            "1.0.0-alpha",
            "1.0.0-alpha.1",
            "1.0.0-alpha.beta",
            "1.0.0-beta.2",
            "1.0.0-beta.11",
            "1.0.0-rc.1",
            "1.0.0",
        ]
        self.assertEqual(
            sorted(map(SemVer.parse, reversed(ordered))),
            list(map(SemVer.parse, ordered)),
        )
        self.assertTrue(SemVer.parse("1.0.0-alpha.1").is_prerelease)
        self.assertFalse(SemVer.parse("1.0.0+build.7").is_prerelease)

    def test_build_metadata_is_preserved_but_does_not_affect_precedence(self) -> None:
        left = SemVer.parse("1.2.3+build.7")
        right = SemVer.parse("1.2.3+build.9")

        self.assertEqual(left, right)
        self.assertEqual(str(left), "1.2.3+build.7")

    def test_invalid_versions_are_rejected(self) -> None:
        for value in ("v1.2.3", "01.2.3", "1.2.3-01", "1.2", "1.2.3-"):
            with self.subTest(value=value), self.assertRaises(VersionError):
                SemVer.parse(value)

    def test_tag_requires_v_prefix(self) -> None:
        self.assertEqual(str(SemVer.from_tag("v1.2.3-rc.1")), "1.2.3-rc.1")
        with self.assertRaises(VersionError):
            SemVer.from_tag("1.2.3")

    def test_device_version_accepts_v_prefix_and_git_describe(self) -> None:
        cases = {
            "1.2.3": "1.2.3",
            "v1.2.3": "1.2.3",
            "v1.2.3-26-g921badd": "1.2.3+26.g921badd",
            "v1.2.3-26-g921badd-dirty": "1.2.3+26.g921badd.dirty",
        }
        for value, expected in cases.items():
            with self.subTest(value=value):
                self.assertEqual(str(SemVer.from_device(value)), expected)

    def test_device_git_describe_does_not_make_same_release_newer(self) -> None:
        current = SemVer.from_device("v1.2.3-26-g921badd")

        self.assertFalse(SemVer.parse("1.2.3") > current)
        self.assertTrue(SemVer.parse("1.2.4") > current)

    def test_device_version_still_rejects_unknown_formats(self) -> None:
        for value in ("development", "release-1.2.3", "v1.2"):
            with self.subTest(value=value), self.assertRaises(VersionError):
                SemVer.from_device(value)


class VersionEntryPointTests(unittest.TestCase):
    def test_package_metadata_uses_the_runtime_version_source(self) -> None:
        project = tomllib.loads((ROOT / "bridge/pyproject.toml").read_text("utf-8"))

        self.assertEqual(project["project"]["dynamic"], ["version"])
        self.assertNotIn("version", project["project"])
        self.assertEqual(
            project["tool"]["setuptools"]["dynamic"]["version"],
            {"attr": "quotaframe_bridge.__version__"},
        )
        self.assertEqual(str(SemVer.parse(__version__)), __version__)

    def test_module_version_flag_prints_the_product_version(self) -> None:
        result = subprocess.run(
            [sys.executable, "-m", "quotaframe_bridge", "--version"],
            check=False,
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertRegex(
            result.stdout,
            r"^QuotaFrame Bridge [0-9]+\.[0-9]+\.[0-9]+(?:[-+].+)?\n$",
        )


if __name__ == "__main__":
    unittest.main()
