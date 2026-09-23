from __future__ import annotations

import argparse
import hashlib
import json
import tempfile
import unittest
from dataclasses import replace
from pathlib import Path
from unittest import mock

from quotaframe_bridge.targets import TARGETS
from scripts.assemble_web_flasher import WebAssemblyError, assemble


class WebFlasherAssemblerTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.release_assets = self.root / "release-assets"
        self.web_root = self.root / "web"
        self.vendor_root = self.root / "vendor"
        self.output = self.root / "site"

        self.release_assets.mkdir()
        (self.web_root / "dist" / "assets").mkdir(parents=True)
        (self.web_root / "dist" / "assets" / "devices").mkdir(parents=True)
        self.vendor_root.mkdir()
        (self.web_root / "dist" / "assets" / "flash-engine.js").write_text("export {};\n", "utf-8")
        (self.web_root / "dist" / "THIRD-PARTY-NOTICES").write_text("Notices\n", "utf-8")

        (self.web_root / "dist" / "devices.json").write_text('{"targets": []}\n', "utf-8")
        (self.web_root / "dist" / "targets.json").write_text('{"version": null, "targets": []}\n', "utf-8")
        (self.web_root / "dist" / "index.html").write_text("<main>flasher</main>\n", "utf-8")
        (self.web_root / "dist" / "assets" / "app.js").write_text("export {};\n", "utf-8")
        (self.web_root / "dist" / "assets" / "styles.css").write_text(
            "main { color: white; }\n", "utf-8"
        )
        (self.vendor_root / "LICENSE").write_text("Apache License 2.0\n", "utf-8")

        for target in TARGETS:
            if target.web_flash is None:
                continue
            (self.web_root / "dist" / target.web_flash.asset).write_text(
                f"photo:{target.id}\n", "utf-8"
            )
            filename = f"{target.release_stem}-full-v1.2.3.bin"
            (self.release_assets / filename).write_bytes(
                f"full:{target.id}".encode("ascii")
            )

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def arguments(self, **overrides: object) -> argparse.Namespace:
        values = {
            "version": "1.2.3",
            "release_assets": self.release_assets,
            "web_root": self.web_root,
            "vendor_root": self.vendor_root,
            "output": self.output,
        }
        values.update(overrides)
        return argparse.Namespace(**values)

    def test_assembles_target_specific_full_images_and_manifests(self) -> None:
        result = assemble(self.arguments())

        self.assertEqual(result, self.output.resolve())
        self.assertEqual(
            (self.output / "firmware" / "m5sticks3-full-v1.2.3.bin").read_bytes(),
            b"full:m5sticks3",
        )
        manifest = json.loads(
            (self.output / "manifests" / "m5sticks3.json").read_text("utf-8")
        )
        self.assertEqual(
            manifest,
            {
                "name": "M5StickS3",
                "version": "1.2.3",
                "new_install_prompt_erase": True,
                "new_install_improv_wait_time": 0,
                "builds": [
                    {
                        "chipFamily": "ESP32-S3",
                        "parts": [
                            {
                                "path": "../firmware/m5sticks3-full-v1.2.3.bin",
                                "offset": 0,
                                "sha256": hashlib.sha256(
                                    b"full:m5sticks3"
                                ).hexdigest(),
                            }
                        ],
                    }
                ],
            },
        )
        self.assertFalse(any(self.output.rglob("*-ota-*.bin")))

    def test_every_manifest_digest_matches_the_image_it_ships(self) -> None:
        assemble(self.arguments())

        for manifest_path in (self.output / "manifests").iterdir():
            manifest = json.loads(manifest_path.read_text("utf-8"))
            for build in manifest["builds"]:
                for part in build["parts"]:
                    image = (manifest_path.parent / part["path"]).resolve()
                    self.assertEqual(
                        hashlib.sha256(image.read_bytes()).hexdigest(),
                        part["sha256"],
                        manifest_path.name,
                    )

    def test_public_target_document_contains_only_browser_fields(self) -> None:
        assemble(self.arguments())

        document = json.loads((self.output / "targets.json").read_text("utf-8"))
        self.assertEqual(document["version"], "1.2.3")
        self.assertEqual(
            document["targets"],
            [
                {
                    "id": "m5sticks3",
                    "name": "M5StickS3",
                    "description": "135 × 240 显示屏 · 正面按键翻页",
                    "asset": "assets/devices/m5sticks3.jpg",
                    "image": "m5sticks3-full-v1.2.3.bin",
                    "manifest": "manifests/m5sticks3.json",
                },
                {
                    "id": "waveshare_amoled_216",
                    "name": "ESP32-S3-Touch-AMOLED-2.16",
                    "description": "480 × 480 圆角方形 AMOLED · 触控与滑动操作",
                    "asset": "assets/devices/waveshare_amoled_216.jpg",
                    "image": "waveshare-esp32-s3-touch-amoled-216-full-v1.2.3.bin",
                    "manifest": "manifests/waveshare_amoled_216.json",
                },
                {
                    "id": "waveshare_epaper_397",
                    "name": "ESP32-S3-ePaper-3.97",
                    "description": "800 × 480 四灰阶墨水屏 · 三向拨轮操作",
                    "asset": "assets/devices/waveshare_epaper_397.jpg",
                    "image": "waveshare-esp32-s3-epaper-397-full-v1.2.3.bin",
                    "manifest": "manifests/waveshare_epaper_397.json",
                },
                {
                    "id": "esp_mosaico",
                    "name": "ESP-Mosaico",
                    "description": "480 × 480 AMOLED · 触控与 AI 按键翻页",
                    "asset": "assets/devices/esp_mosaico.png",
                    "image": "espressif-esp-mosaico-full-v1.2.3.bin",
                    "manifest": "manifests/esp_mosaico.json",
                },
            ],
        )

    def test_targets_without_web_metadata_are_left_out(self) -> None:
        enabled, disabled = TARGETS[0], replace(TARGETS[1], web_flash=None)
        with mock.patch(
            "scripts.assemble_web_flasher.TARGETS", (enabled, disabled)
        ):
            assemble(self.arguments())

        document = json.loads((self.output / "targets.json").read_text("utf-8"))
        self.assertEqual(
            [entry["id"] for entry in document["targets"]], [enabled.id]
        )
        self.assertFalse((self.output / "manifests" / f"{disabled.id}.json").exists())
        self.assertFalse(
            any(self.output.rglob(f"{disabled.release_stem}-full-*.bin"))
        )

    def test_copies_frontend_vendor_and_license(self) -> None:
        docs = self.web_root / "dist" / "docs"
        docs.mkdir()
        (docs / "index.html").write_text("<main>Guide</main>\n", "utf-8")
        assemble(self.arguments())
        self.assertEqual(
            (self.output / "docs" / "index.html").read_text("utf-8"),
            "<main>Guide</main>\n",
        )

        self.assertEqual(
            (self.output / "index.html").read_text("utf-8"),
            "<main>flasher</main>\n",
        )
        self.assertEqual(
            (self.output / "assets" / "app.js").read_text("utf-8"),
            "export {};\n",
        )
        self.assertTrue(
            (self.output / "assets" / "devices" / "waveshare_amoled_216.jpg")
            .is_file()
        )
        self.assertTrue(
            (self.output / "assets" / "flash-engine.js")
            .is_file()
        )
        self.assertEqual(
            (
                self.output / "vendor" / "esp-web-tools" / "LICENSE"
            ).read_text("utf-8"),
            "Apache License 2.0\n",
        )

    def test_missing_required_input_preserves_existing_output(self) -> None:
        missing = self.release_assets / "m5sticks3-full-v1.2.3.bin"
        missing.unlink()
        self.output.mkdir()
        marker = self.output / "keep.txt"
        marker.write_text("preserve\n", "utf-8")

        with self.assertRaisesRegex(WebAssemblyError, "missing required input"):
            assemble(self.arguments())

        self.assertEqual(marker.read_text("utf-8"), "preserve\n")

    def test_missing_engine_build_preserves_existing_output(self) -> None:
        (self.web_root / "dist" / "assets" / "flash-engine.js").unlink()
        self.output.mkdir()
        marker = self.output / "keep.txt"
        marker.write_text("preserve", "utf-8")
        with self.assertRaisesRegex(WebAssemblyError, "bundled flash engine"):
            assemble(self.arguments())
        self.assertEqual(marker.read_text("utf-8"), "preserve")

    def test_missing_device_art_or_vendor_license_is_rejected(self) -> None:
        waveshare = self.web_root / "dist/assets/devices/waveshare_amoled_216.jpg"
        waveshare.unlink()
        with self.assertRaisesRegex(WebAssemblyError, "device art"):
            assemble(self.arguments())

        waveshare.write_text("photo\n", "utf-8")
        (self.vendor_root / "LICENSE").unlink()
        with self.assertRaisesRegex(WebAssemblyError, "vendor license"):
            assemble(self.arguments())

    def test_invalid_version_and_broad_output_are_rejected(self) -> None:
        with self.assertRaisesRegex(WebAssemblyError, "canonical SemVer"):
            assemble(self.arguments(version="v1.2.3"))
        with self.assertRaisesRegex(WebAssemblyError, "too broad"):
            assemble(self.arguments(output=Path.cwd()))

    def test_successful_assembly_replaces_stale_output(self) -> None:
        self.output.mkdir()
        (self.output / "stale.txt").write_text("old\n", "utf-8")

        assemble(self.arguments())

        self.assertFalse((self.output / "stale.txt").exists())

    def test_mid_assembly_failure_preserves_previous_site(self) -> None:
        self.output.mkdir()
        marker = self.output / "keep.txt"
        marker.write_text("previous site\n", "utf-8")

        from scripts import assemble_web_flasher

        copyfile = assemble_web_flasher.shutil.copyfile

        def fail_on_application(
            source: Path,
            destination: Path,
            **kwargs: object,
        ) -> None:
            if Path(source).name == "app.js":
                raise OSError("simulated copy failure")
            copyfile(source, destination, **kwargs)

        with mock.patch.object(
            assemble_web_flasher.shutil,
            "copyfile",
            side_effect=fail_on_application,
        ):
            with self.assertRaisesRegex(OSError, "simulated copy failure"):
                assemble(self.arguments())

        self.assertEqual(marker.read_text("utf-8"), "previous site\n")
        self.assertEqual(list(self.root.glob(".site-*")), [])


if __name__ == "__main__":
    unittest.main()
