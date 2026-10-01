from __future__ import annotations

import json
import subprocess
import sys
import tempfile
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
                "Waveshare AMOLED 2.16",
                "firmware/targets/esp32_s3_touch_amoled_216",
                "6.1",
                "ws_usage_panel.bin",
                "ws_usage_panel_full.bin",
                "waveshare-esp32-s3-touch-amoled-216",
            ),
            "waveshare_epaper_397": (
                "Waveshare ePaper 3.97",
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
            "waveshare_rlcd_42": (
                "Waveshare RLCD 4.2",
                "firmware/targets/waveshare_rlcd_42",
                "6.1",
                "ws_rlcd_42.bin",
                "ws_rlcd_42_full.bin",
                "waveshare-esp32-s3-rlcd-42",
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
                "waveshare_rlcd_42": 9,
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
        paths = {entry["path"] for entry in entries}
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

    def test_ci_names_preserve_required_status_checks(self) -> None:
        names = {entry["name"] for entry in target_registry.ci_matrix()["include"]}
        self.assertTrue({
            "ESP32-S3-Touch-AMOLED-2.16 target",
            "ESP32-S3-Touch-AMOLED-2.16 logic tests",
            "ESP32-S3-ePaper-3.97 target",
            "ESP32-S3-RLCD-4.2 target",
            "ESP32-S3-RLCD-4.2 logic tests",
        } <= names)

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
                    if path == entry["path"]
                },
                {"6.1", "6.2"},
            )
        future_entries = [
            entry
            for entry in entries
            if entry["path"].startswith("firmware/targets/future_target")
        ]
        self.assertEqual(
            {entry["idf_version"] for entry in future_entries},
            {"6.2"},
        )
        self.assertEqual(
            len({entry["cache-name"] for entry in entries}),
            len(entries),
        )

    def test_ci_matrix_runs_one_job_per_project(self) -> None:
        entries = self._matrix("ci-matrix")["include"]
        test_apps = [
            "firmware/test_apps/protocol",
            "firmware/test_apps/ota",
            "firmware/test_apps/panel_state",
            *(app for target in TARGETS for app in target.test_apps),
        ]
        self.assertEqual(len(entries), len(test_apps) + len(TARGETS))
        self.assertCountEqual(
            [entry["path"] for entry in entries if not entry["check-lockfile"]],
            test_apps,
        )
        for field in ("name", "cache-name", "path"):
            self.assertEqual(len({entry[field] for entry in entries}), len(entries), field)
        for target in TARGETS:
            built = [
                entry
                for entry in entries
                if entry["check-lockfile"]
                and entry["path"] == target.firmware_project
            ]
            self.assertEqual(len(built), 1, target.id)

    def test_ci_plan_selects_affected_checks_without_losing_required_names(self) -> None:
        entries = target_registry.ci_matrix()["include"]
        all_paths = {entry["path"] for entry in entries}
        all_names = {entry["name"] for entry in entries}
        rlcd = "firmware/targets/waveshare_rlcd_42"
        cases = [
            ([], False, False, set()),
            (["README.md", "docs/PORTING.md", ".gitignore"], False, False, set()),
            ([f"{rlcd}/README.md"], False, False, set()),
            (["bridge/src/quotaframe_bridge/device_manager.py"], True, False, set()),
            (["bridge/src/quotaframe_bridge/__init__.py"], True, True, set()),
            (["web/src/app.js"], False, True, set()),
            (["web/src/content/guide.md"], False, True, set()),
            (["scripts/export_web_devices.py"], True, True, set()),
            ([f"{rlcd}/main/screens.cpp"], True, False, {rlcd, f"{rlcd}/test_apps/logic"}),
            ([f"{rlcd}/test_apps/logic/main/test_screens.cpp"], True, False, {f"{rlcd}/test_apps/logic"}),
            (["firmware/test_apps/ota/main/test_ota_session.cpp"], True, False, {"firmware/test_apps/ota"}),
            (["firmware/components/usage_core/CMakeLists.txt"], True, False, all_paths),
            (["firmware/targets/unknown/main.cpp"], True, False, all_paths),
            (["protocol/examples/usage-ok.jsonl"], True, False, all_paths),
            (["scripts/target_registry.py"], True, True, all_paths),
            (["bridge/src/quotaframe_bridge/targets.py"], True, True, all_paths),
            ([".github/workflows/ci.yml"], True, True, all_paths),
            (["new-build-config.toml"], True, True, all_paths),
        ]
        for paths, bridge, web, expected in cases:
            with self.subTest(paths=paths):
                plan = target_registry.ci_plan(paths)
                built = plan["matrix"]["include"]
                skipped = plan["skipped_matrix"]["include"]
                self.assertEqual((plan["bridge"], plan["web"]), (bridge, web))
                self.assertEqual({entry["path"] for entry in built}, expected)
                names = [entry["name"] for entry in built + skipped]
                self.assertCountEqual(names, all_names)
                self.assertEqual((plan["firmware"], plan["skipped"]), (bool(built), bool(skipped)))

    def test_ci_plan_cli_accepts_deleted_and_renamed_paths(self) -> None:
        old = "firmware/targets/m5sticks3/main/old name.cpp"
        new = "firmware/targets/waveshare_rlcd_42/main/new name.cpp"
        with tempfile.TemporaryDirectory() as directory:
            paths = Path(directory) / "changed-files"
            # Git --no-renames reports both sides even when the old file is absent.
            paths.write_bytes(f"{old}\0{new}\0".encode())
            completed = subprocess.run(
                [sys.executable, str(REPOSITORY_ROOT / "scripts/target_registry.py"),
                 "ci-plan", "--changed-files", str(paths)],
                check=True, capture_output=True, text=True,
            )
        plan = {key: json.loads(value) for key, value in
                (line.split("=", 1) for line in completed.stdout.splitlines())}
        self.assertEqual(
            {entry["path"] for entry in plan["matrix"]["include"]},
            {"firmware/targets/m5sticks3", "firmware/targets/m5sticks3/test_apps/logic",
             "firmware/targets/waveshare_rlcd_42", "firmware/targets/waveshare_rlcd_42/test_apps/logic"},
        )

    def test_new_web_targets_match_chip_families(self) -> None:
        for target_id, chip in (
            ("waveshare_epaper_397", "ESP32-S3"),
            ("esp_mosaico", "ESP32-S31"),
            ("waveshare_rlcd_42", "ESP32-S3"),
        ):
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
                "Waveshare AMOLED 2.16",
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
