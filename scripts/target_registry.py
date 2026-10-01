#!/usr/bin/env python3
"""Emit repository build matrices from the maintained target registry."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "bridge" / "src"))

from quotaframe_bridge.targets import IDF_IMAGES, TARGETS  # noqa: E402


# Branch protection requires these check names independently of display labels.
_CI_LABELS = {
    "waveshare_amoled_216": "ESP32-S3-Touch-AMOLED-2.16",
    "waveshare_epaper_397": "ESP32-S3-ePaper-3.97",
    "waveshare_rlcd_42": "ESP32-S3-RLCD-4.2",
}


def toolchain(version: str) -> dict[str, str]:
    return {
        "idf_version": version,
        "idf_image": IDF_IMAGES[version],
    }


def release_matrix() -> dict[str, list[dict[str, object]]]:
    return {
        "include": [
            {
                "id": target.id,
                "label": target.label,
                "path": target.firmware_project,
                **toolchain(target.idf_version),
                "image": target.ota_image,
                "full_image": target.full_image,
                "release_stem": target.release_stem,
            }
            for target in TARGETS
        ]
    }


def ci_matrix() -> dict[str, list[dict[str, object]]]:
    versions = tuple(dict.fromkeys(target.idf_version for target in TARGETS))
    entries: list[dict[str, object]] = []
    for version in versions:
        suffix = "" if len(versions) == 1 else f" (IDF {version})"
        cache_suffix = "" if len(versions) == 1 else f"-idf-{version}"
        test_apps = [
            ("protocol tests", "firmware/test_apps/protocol"),
            ("ota tests", "firmware/test_apps/ota"),
            ("panel_state tests", "firmware/test_apps/panel_state"),
        ]
        test_apps.extend(
            (f"{_CI_LABELS.get(target.id, target.label)} {Path(app).name} tests", app)
            for target in TARGETS
            if target.idf_version == version
            for app in target.test_apps
        )
        entries.extend(
            {
                "name": f"{name}{suffix}",
                "path": app,
                "cache-name": f"{app.replace('/', '-').replace('_', '-')}{cache_suffix}",
                "check-lockfile": False,
                **toolchain(version),
            }
            for name, app in test_apps
        )
    for target in TARGETS:
        entries.append(
            {
                "name": f"{_CI_LABELS.get(target.id, target.label)} target",
                "path": target.firmware_project,
                "cache-name": target.id.replace("_", "-"),
                "check-lockfile": True,
                **toolchain(target.idf_version),
            }
        )
    return {"include": entries}


def ci_plan(changed_files: list[str]) -> dict[str, object]:
    """Select builds conservatively and retain unselected required check names."""
    entries = ci_matrix()["include"]
    all_paths = {entry["path"] for entry in entries}
    test_paths = {entry["path"] for entry in entries if not entry["check-lockfile"]}
    selected: set[str] = set()
    bridge = web = False
    for path in changed_files:
        if path.startswith("web/"):
            web = True
            continue
        # Release notes and licence notices are inputs to the Bridge tests.
        if path in {"LICENSE", "THIRD_PARTY_LICENSES.md"} or path.startswith("docs/release/notes/"):
            bridge = True
            continue
        if path.endswith(".md") or path.startswith("docs/") or path in {".gitignore", ".gitattributes"}:
            continue
        if path in {
            "scripts/target_registry.py",
            "bridge/src/quotaframe_bridge/targets.py",
        } or path.startswith(".github/workflows/"):
            bridge = web = True
            selected.update(all_paths)
        elif path.startswith("firmware/"):
            # The Bridge suite also checks firmware layout and test registration.
            bridge = True
            app = next((app for app in test_paths if path.startswith(app + "/")), None)
            target = next(
                (target for target in TARGETS if path.startswith(target.firmware_project + "/")),
                None,
            )
            if app is not None:
                selected.add(app)
            elif target is not None:
                selected.update((target.firmware_project, *target.test_apps))
            else:
                # Shared components and unregistered firmware paths require full coverage.
                selected.update(all_paths)
        elif path.startswith("protocol/"):
            bridge = True
            selected.update(all_paths)
        elif path.startswith("bridge/"):
            bridge = True
            web = web or path == "bridge/src/quotaframe_bridge/__init__.py"
        elif path.startswith("scripts/"):
            bridge = web = True
        else:
            # Unknown build inputs must not silently bypass validation.
            bridge = web = True
            selected.update(all_paths)
    builds = [entry for entry in entries if entry["path"] in selected]
    skipped = [{"name": entry["name"]} for entry in entries if entry["path"] not in selected]
    return {
        "bridge": bridge,
        "web": web,
        "firmware": bool(builds),
        "skipped": bool(skipped),
        "matrix": {"include": builds},
        "skipped_matrix": {"include": skipped},
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("command", choices=("ci-matrix", "release-matrix", "ci-plan"))
    parser.add_argument("--changed-files", type=Path, help="NUL-delimited changed paths from git diff")
    args = parser.parse_args()
    if args.command == "ci-plan":
        if args.changed_files is None:
            parser.error("ci-plan requires --changed-files")
        paths = args.changed_files.read_bytes().decode("utf-8", errors="surrogateescape").split("\0")
        for key, value in ci_plan([path for path in paths if path]).items():
            print(f"{key}={json.dumps(value, separators=(',', ':'))}")
        return 0
    matrix = ci_matrix() if args.command == "ci-matrix" else release_matrix()
    print(json.dumps(matrix, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
