from __future__ import annotations

import json
import subprocess
import sys
import unittest
from dataclasses import replace
from pathlib import Path
from unittest.mock import patch

from quotaframe_bridge.targets import TARGETS, TARGETS_BY_ID
from scripts import target_registry


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]


class TargetRegistryTests(unittest.TestCase):
    def test_preserves_existing_maintained_target_contracts(self) -> None:
        expected = {
            "m5sticks3": (
                "M5StickS3",
                "firmware/targets/m5sticks3",
                "6.1",
                "m5_usage_panel.bin",
                "m5_usage_panel_full.bin",
                "m5sticks3",
            ),
            "waveshare_amoled_216": (
                "ESP32-S3-Touch-AMOLED-2.16",
                "firmware/targets/esp32_s3_touch_amoled_216",
                "6.1",
                "ws_usage_panel.bin",
                "ws_usage_panel_full.bin",
                "waveshare-esp32-s3-touch-amoled-216",
            ),
            "waveshare_epaper_397": (
                "ESP32-S3-ePaper-3.97",
                "firmware/targets/waveshare_epaper_397",
                "6.1",
                "ws_epaper_397.bin",
                "ws_epaper_397_full.bin",
                "waveshare-esp32-s3-epaper-397",
            ),
            "esp_mosaico": (
                "ESP-Mosaico",
                "firmware/targets/esp_mosaico",
                "6.1",
                "mosaico_usage_panel.bin",
                "mosaico_usage_panel_full.bin",
                "espressif-esp-mosaico",
            ),
        }
        self.assertEqual(set(expected), set(TARGETS_BY_ID))
        self.assertEqual(
            {target.id: target.image_chip_id for target in TARGETS},
            {
                "m5sticks3": 9,
                "waveshare_amoled_216": 9,
                "waveshare_epaper_397": 9,
                "esp_mosaico": 32,
            },
        )
        for target_id, contract in expected.items():
            target = TARGETS_BY_ID[target_id]
            self.assertEqual(
                (
                    target.label,
                    target.firmware_project,
                    target.idf_version,
                    target.ota_image,
                    target.full_image,
                    target.release_stem,
                ),
                contract,
            )

    def test_registry_values_are_unique(self) -> None:
        for field in (
            "id",
            "label",
            "firmware_project",
            "release_stem",
        ):
            values = [getattr(target, field) for target in TARGETS]
            self.assertEqual(len(values), len(set(values)), field)

    def test_production_projects_follow_uniform_target_layout(self) -> None:
        for target in TARGETS:
            project = REPOSITORY_ROOT / target.firmware_project
            self.assertTrue(project.is_dir(), target.id)
            self.assertEqual(project.parent, REPOSITORY_ROOT / "firmware/targets")
            for filename in (
                "CMakeLists.txt",
                "dependencies.lock",
                "partitions.csv",
                "sdkconfig.defaults",
            ):
                self.assertTrue((project / filename).is_file(), f"{target.id}: {filename}")
        self.assertFalse((REPOSITORY_ROOT / "firmware/main").exists())
        self.assertFalse((REPOSITORY_ROOT / "firmware/CMakeLists.txt").exists())
        self.assertFalse((REPOSITORY_ROOT / "firmware/dependencies.lock").exists())
        self.assertFalse((REPOSITORY_ROOT / "firmware/partitions.csv").exists())
        self.assertFalse((REPOSITORY_ROOT / "firmware/sdkconfig.defaults").exists())

    def _matrix(self, command: str) -> dict[str, list[dict[str, object]]]:
        helper = REPOSITORY_ROOT / "scripts/target_registry.py"
        completed = subprocess.run(
            [sys.executable, str(helper), command],
            check=True,
            capture_output=True,
            text=True,
            cwd=REPOSITORY_ROOT,
        )
        return json.loads(completed.stdout)

    def test_release_matrix_uses_registry(self) -> None:
        matrix = self._matrix("release-matrix")
        self.assertEqual(
            {entry["id"] for entry in matrix["include"]},
            {target.id for target in TARGETS},
        )
        for entry in matrix["include"]:
            target = TARGETS_BY_ID[entry["id"]]
            self.assertEqual(entry["path"], target.firmware_project)
            self.assertEqual(entry["idf_version"], "6.1")
            self.assertEqual(
                entry["idf_image"],
                "espressif/idf:v6.1@sha256:"
                "81893c71bb5e570088901f21def8684c25cd2a9020281bd01b843a7655edb18c",
            )
            self.assertEqual(entry["image"], target.ota_image)
            self.assertEqual(entry["full_image"], target.full_image)
            self.assertEqual(entry["release_stem"], target.release_stem)

    def test_ci_matrix_includes_registered_projects_and_target_tests(self) -> None:
        matrix = self._matrix("ci-matrix")
        entries = matrix["include"]
        paths = {path for entry in entries for path in entry["paths"].split()}
        expected_image = (
            "espressif/idf:v6.1@sha256:"
            "81893c71bb5e570088901f21def8684c25cd2a9020281bd01b843a7655edb18c"
        )
        self.assertTrue(entries)
        self.assertEqual({entry["idf_version"] for entry in entries}, {"6.1"})
        self.assertEqual({entry["idf_image"] for entry in entries}, {expected_image})
        for target in TARGETS:
            self.assertIn(target.firmware_project, paths)
            for test_app in target.test_apps:
                self.assertIn(test_app, paths)

    def test_ci_matrix_builds_shared_apps_for_every_registered_idf_version(self) -> None:
        future = replace(
            TARGETS[1],
            id="future_target",
            firmware_project="firmware/targets/future_target",
            idf_version="6.2",
            test_apps=("firmware/targets/future_target/test_apps/logic",),
        )
        images = {
            "6.1": "example.invalid/idf:6.1@sha256:old",
            "6.2": "example.invalid/idf:6.2@sha256:new",
        }
        with (
            patch.object(target_registry, "TARGETS", (TARGETS[0], future)),
            patch.object(target_registry, "IDF_IMAGES", images),
        ):
            entries = target_registry.ci_matrix()["include"]

        shared_paths = {
            "firmware/test_apps/protocol",
            "firmware/test_apps/ota",
            "firmware/test_apps/panel_state",
        }
        for path in shared_paths:
            self.assertEqual(
                {
                    entry["idf_version"]
                    for entry in entries
                    if path in entry["paths"].split()
                },
                {"6.1", "6.2"},
            )
        future_entries = [
            entry
            for entry in entries
            if any(
                path.startswith("firmware/targets/future_target")
                for path in entry["paths"].split()
            )
        ]
        self.assertEqual(
            {entry["idf_version"] for entry in future_entries},
            {"6.2"},
        )
        self.assertEqual(
            len({entry["cache-name"] for entry in entries}),
            len(entries),
        )

    def test_ci_matrix_runs_one_job_per_toolchain_and_target(self) -> None:
        # Actions bills each job's container pull and cache restore separately,
        # so the short test-application builds share a single job.
        entries = self._matrix("ci-matrix")["include"]
        self.assertEqual(len(entries), 1 + len(TARGETS))
        shared = [entry for entry in entries if not entry["check-lockfile"]]
        self.assertEqual(len(shared), 1)
        self.assertEqual(
            shared[0]["paths"].split(),
            [
                "firmware/test_apps/protocol",
                "firmware/test_apps/ota",
                "firmware/test_apps/panel_state",
                *(app for target in TARGETS for app in target.test_apps),
            ],
        )
        for target in TARGETS:
            built = [
                entry
                for entry in entries
                if entry["check-lockfile"]
                and entry["paths"] == target.firmware_project
            ]
            self.assertEqual(len(built), 1, target.id)

    def test_new_web_targets_match_chip_families(self) -> None:
        for target_id, chip in (("waveshare_epaper_397", "ESP32-S3"), ("esp_mosaico", "ESP32-S31")):
            web = TARGETS_BY_ID[target_id].web_flash
            self.assertIsNotNone(web)
            self.assertEqual(web.chip_family, chip)

    def test_web_target_metadata_covers_current_hardware(self) -> None:
        expected = {
            "m5sticks3": (
                "M5StickS3",
                "135 × 240 显示屏 · 正面按键翻页",
                "ESP32-S3",
                "assets/devices/m5sticks3.jpg",
            ),
            "waveshare_amoled_216": (
                "ESP32-S3-Touch-AMOLED-2.16",
                "480 × 480 圆角方形 AMOLED · 触控与滑动操作",
                "ESP32-S3",
                "assets/devices/waveshare_amoled_216.jpg",
            ),
        }
        for target_id, contract in expected.items():
            web = TARGETS_BY_ID[target_id].web_flash
            self.assertIsNotNone(web)
            self.assertEqual(
                (web.name, web.description, web.chip_family, web.asset),
                contract,
            )


if __name__ == "__main__":
    unittest.main()
