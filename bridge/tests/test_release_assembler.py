from __future__ import annotations

import hashlib
import json
import subprocess
import struct
import sys
import tempfile
import unittest
from pathlib import Path

from quotaframe_bridge.targets import TARGETS, TARGETS_BY_ID


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "assemble_release.py"
VERSION = "0.5.0"
REPOSITORY = "eMUQI/QuotaFrame"


class ReleaseAssemblerTests(unittest.TestCase):
    def setUp(self) -> None:
        self._temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self._temporary.cleanup)
        self.root = Path(self._temporary.name)
        self.fixture = self.root / "fixture"
        self.fixture.mkdir()
        self.firmware_root = self.root / "firmware"
        self.output = self.root / "out"
        self.contents = {
            "bridge.exe": b"windows bridge",
            "setup.exe": b"windows installer",
            "bridge.dmg": b"macOS disk image",
            "LICENSE": b"MPL-2.0\n",
            "THIRD_PARTY_LICENSES.md": b"notices\n",
        }
        for name, content in self.contents.items():
            (self.fixture / name).write_bytes(content)

        self.firmware_contents: dict[str, tuple[bytes, bytes]] = {}
        for target in TARGETS:
            artifact = self._artifact(target.id)
            artifact.mkdir(parents=True)
            image = bytearray(320)
            image[0] = 0xE9
            struct.pack_into("<H", image, 12, target.image_chip_id)
            struct.pack_into("<I", image, 32, 0xABCD5432)
            image[48:48 + len(VERSION)] = VERSION.encode("ascii")
            project = Path(target.ota_image).stem.encode("ascii")
            image[80:80 + len(project)] = project
            ota = bytes(image)
            full = b"\xff" * 0x10000 + ota
            (artifact / target.ota_image).write_bytes(ota)
            (artifact / target.full_image).write_bytes(full)
            self._write_description(target.id, VERSION)
            self.firmware_contents[target.id] = (ota, full)

    def _artifact(self, target_id: str) -> Path:
        return self.firmware_root / f"firmware-{target_id}"

    def _write_description(self, target_id: str, version: str) -> None:
        (self._artifact(target_id) / "project_description.json").write_text(
            json.dumps({"project_version": version}),
            encoding="utf-8",
        )

    def _command(self, **overrides: str) -> list[str]:
        values = {
            "version": VERSION,
            "repository": REPOSITORY,
            "firmware-root": str(self.firmware_root),
            "windows-exe": str(self.fixture / "bridge.exe"),
            "windows-setup": str(self.fixture / "setup.exe"),
            "macos-dmg": str(self.fixture / "bridge.dmg"),
            "license": str(self.fixture / "LICENSE"),
            "third-party-licenses": str(
                self.fixture / "THIRD_PARTY_LICENSES.md"
            ),
            "output": str(self.output),
        }
        values.update(overrides)
        command = [sys.executable, str(SCRIPT)]
        for name, value in values.items():
            command.extend((f"--{name}", value))
        return command

    def _run(self, **overrides: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            self._command(**overrides),
            capture_output=True,
            text=True,
            timeout=30,
            check=False,
        )

    def test_emits_exact_assets_manifest_and_sorted_checksums(self) -> None:
        completed = self._run()

        self.assertEqual(completed.returncode, 0, completed.stderr)
        expected_assets = {
            "quotaframe-bridge-windows-v0.5.0.exe",
            "quotaframe-bridge-windows-v0.5.0-setup.exe",
            "quotaframe-bridge-macos-arm64-v0.5.0.dmg",
            "manifest.json",
            "SHA256SUMS.txt",
            "LICENSE",
            "THIRD_PARTY_LICENSES.md",
        }
        for target in TARGETS:
            expected_assets.add(f"{target.release_stem}-ota-v{VERSION}.bin")
            expected_assets.add(f"{target.release_stem}-full-v{VERSION}.bin")
        self.assertEqual({path.name for path in self.output.iterdir()}, expected_assets)

        copied = {
            "quotaframe-bridge-windows-v0.5.0.exe": self.contents["bridge.exe"],
            "quotaframe-bridge-windows-v0.5.0-setup.exe": self.contents["setup.exe"],
            "quotaframe-bridge-macos-arm64-v0.5.0.dmg": self.contents[
                "bridge.dmg"
            ],
        }
        for target in TARGETS:
            ota, full = self.firmware_contents[target.id]
            copied[f"{target.release_stem}-ota-v{VERSION}.bin"] = ota
            copied[f"{target.release_stem}-full-v{VERSION}.bin"] = full
        for name, content in copied.items():
            self.assertEqual((self.output / name).read_bytes(), content)

        manifest_bytes = (self.output / "manifest.json").read_bytes()
        manifest = json.loads(manifest_bytes)
        self.assertNotIn("full", manifest_bytes.decode("utf-8"))
        self.assertEqual(
            manifest_bytes,
            json.dumps(manifest, separators=(",", ":")).encode("utf-8") + b"\n",
        )
        self.assertEqual(manifest["schema_version"], 1)
        self.assertEqual(
            [image["target"] for image in manifest["releases"]],
            [target.id for target in TARGETS],
        )
        for image, target in zip(manifest["releases"], TARGETS, strict=True):
            ota, _full = self.firmware_contents[target.id]
            filename = f"{target.release_stem}-ota-v{VERSION}.bin"
            self.assertEqual(image["target"], target.id)
            self.assertEqual(image["version"], VERSION)
            self.assertEqual(image["size"], len(ota))
            self.assertEqual(image["sha256"], hashlib.sha256(ota).hexdigest())
            self.assertEqual(
                image["url"],
                f"https://github.com/{REPOSITORY}/releases/download/"
                f"v{VERSION}/{filename}",
            )

        checksum_lines = (self.output / "SHA256SUMS.txt").read_text(
            "ascii"
        ).splitlines()
        copied.update({"manifest.json": manifest_bytes, "LICENSE": self.contents["LICENSE"],
                       "THIRD_PARTY_LICENSES.md": self.contents["THIRD_PARTY_LICENSES.md"]})
        self.assertEqual(len(checksum_lines), 6 + 2 * len(TARGETS))
        self.assertEqual(
            checksum_lines,
            [
                f"{hashlib.sha256(copied[name]).hexdigest()}  {name}"
                for name in sorted(copied)
            ],
        )

    def test_rejects_oversize_wrong_identity_and_mismatched_full_image(self):
        target = TARGETS[0]
        artifact = self._artifact(target.id)
        original, full = self.firmware_contents[target.id]
        for kind in ("size", "identity", "full"):
            with self.subTest(kind=kind):
                image = bytearray(original)
                if kind == "size":
                    image.extend(b"x" * target.ota_transfer_bytes)
                elif kind == "identity":
                    image[80] ^= 1
                (artifact / target.ota_image).write_bytes(image)
                (artifact / target.full_image).write_bytes(b"invalid" if kind == "full" else full)
                result = self._run()
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse(self.output.exists())

    def test_accepts_full_partition_images_and_rejects_one_byte_over(self) -> None:
        from scripts.assemble_release import AssemblyError, _validate_firmware

        for target in TARGETS:
            with self.subTest(target=target.id):
                artifact = self._artifact(target.id)
                original, _ = self.firmware_contents[target.id]
                image = original.ljust(target.ota_partition_bytes, b"x")
                sources = {"ota": artifact / target.ota_image,
                           "full": artifact / target.full_image}
                sources["ota"].write_bytes(image)
                sources["full"].write_bytes(b"\xff" * 0x10000 + image)
                _validate_firmware(target, sources, VERSION)
                sources["ota"].write_bytes(image + b"x")
                with self.assertRaisesRegex(AssemblyError, "does not fit both OTA partitions"):
                    _validate_firmware(target, sources, VERSION)

    def test_rejects_wrong_chip_for_each_target(self) -> None:
        for target in TARGETS:
            with self.subTest(target=target.id):
                artifact = self._artifact(target.id)
                original, full = self.firmware_contents[target.id]
                image = bytearray(original)
                struct.pack_into("<H", image, 12, 9 if target.image_chip_id == 32 else 32)
                (artifact / target.ota_image).write_bytes(image)
                (artifact / target.full_image).write_bytes(full[:0x10000] + image)
                result = self._run()
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("OTA image header is invalid", result.stderr)
                self.assertFalse(self.output.exists())
                (artifact / target.ota_image).write_bytes(original)
                (artifact / target.full_image).write_bytes(full)

    def test_rejects_invalid_semver_before_reading_files(self) -> None:
        for version in ("v0.5.0", "01.2.3", "1.2.3-01", "1.2.3-alpha..1"):
            with self.subTest(version=version):
                completed = self._run(
                    version=version,
                    **{"firmware-root": str(self.root / "missing-firmware")},
                )
                self.assertNotEqual(completed.returncode, 0)
                self.assertIn("canonical SemVer", completed.stderr)

    def test_rejects_invalid_repository(self) -> None:
        for repository in ("owner", "../repo", "owner/.."):
            with self.subTest(repository=repository):
                completed = self._run(repository=repository)
                self.assertNotEqual(completed.returncode, 0)
                self.assertIn("owner/name", completed.stderr)

    def test_rejects_missing_input(self) -> None:
        self.output.mkdir()
        sentinel = self.output / "keep-me"
        sentinel.write_text("preserved", encoding="utf-8")
        completed = self._run(
            **{"windows-exe": str(self.fixture / "missing.exe")}
        )

        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("missing required input", completed.stderr)
        self.assertEqual(sentinel.read_text("utf-8"), "preserved")

    def test_rejects_missing_full_firmware_before_replacing_output(self) -> None:
        self.output.mkdir()
        sentinel = self.output / "keep-me"
        sentinel.write_text("preserved", encoding="utf-8")
        target = TARGETS_BY_ID["m5sticks3"]
        (self._artifact(target.id) / target.full_image).unlink()
        completed = self._run()

        self.assertNotEqual(completed.returncode, 0)
        self.assertIn(
            f"missing required input: {target.label} full firmware image",
            completed.stderr,
        )
        self.assertEqual(sentinel.read_text("utf-8"), "preserved")

    def test_rejects_output_directory_that_contains_inputs(self) -> None:
        completed = self._run(output=str(self.fixture))

        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("must not contain an input", completed.stderr)
        self.assertEqual((self.fixture / "bridge.exe").read_bytes(), b"windows bridge")

    def test_rejects_any_firmware_version_mismatch(self) -> None:
        for target in TARGETS:
            with self.subTest(target=target.id):
                self._write_description(target.id, "0.4.0")
                completed = self._run()
                self.assertNotEqual(completed.returncode, 0)
                self.assertIn("does not match release version", completed.stderr)
                self.assertFalse(self.output.exists())
                self._write_description(target.id, VERSION)


if __name__ == "__main__":
    unittest.main()
