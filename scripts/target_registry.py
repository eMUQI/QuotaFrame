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
    # Every test application for a toolchain shares one job: each build is
    # short, while Actions bills the container pull, checkout and cache restore
    # separately for every job it starts.
    for version in versions:
        suffix = "" if len(versions) == 1 else f" (IDF {version})"
        cache_suffix = "" if len(versions) == 1 else f"-idf-{version}"
        test_apps = [
            "firmware/test_apps/protocol",
            "firmware/test_apps/ota",
            "firmware/test_apps/panel_state",
        ]
        test_apps.extend(
            app
            for target in TARGETS
            if target.idf_version == version
            for app in target.test_apps
        )
        entries.append(
            {
                "name": f"test applications{suffix}",
                "paths": " ".join(test_apps),
                "cache-name": f"test-apps{cache_suffix}",
                "check-lockfile": False,
                **toolchain(version),
            }
        )
    for target in TARGETS:
        entries.append(
            {
                "name": f"{target.label} target",
                "paths": target.firmware_project,
                "cache-name": target.id.replace("_", "-"),
                "check-lockfile": True,
                **toolchain(target.idf_version),
            }
        )
    return {"include": entries}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("command", choices=("ci-matrix", "release-matrix"))
    args = parser.parse_args()
    matrix = ci_matrix() if args.command == "ci-matrix" else release_matrix()
    print(json.dumps(matrix, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
