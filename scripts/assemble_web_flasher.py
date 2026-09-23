#!/usr/bin/env python3
"""Assemble a self-contained browser flasher from verified Release assets."""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "bridge" / "src"))

from quotaframe_bridge.targets import TARGETS  # noqa: E402
from quotaframe_bridge.versioning import SemVer, VersionError  # noqa: E402


class WebAssemblyError(ValueError):
    """Web flasher inputs or output path violate the assembly contract."""


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", required=True)
    parser.add_argument("--release-assets", required=True, type=Path)
    parser.add_argument("--web-root", required=True, type=Path)
    parser.add_argument("--vendor-root", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    return parser


def _required_file(path: Path, label: str) -> Path:
    resolved = path.expanduser().resolve()
    if not resolved.is_file():
        raise WebAssemblyError(f"missing required input: {label}")
    return resolved


def _required_directory(path: Path, label: str) -> Path:
    resolved = path.expanduser().resolve()
    if not resolved.is_dir():
        raise WebAssemblyError(f"missing required input: {label}")
    return resolved


def _validate_output(output: Path, inputs: tuple[Path, ...]) -> Path:
    requested = output.expanduser()
    if requested.is_symlink():
        raise WebAssemblyError("output directory must not be a symbolic link")
    resolved = requested.resolve(strict=False)
    forbidden = {
        Path(resolved.anchor),
        Path.home().resolve(),
        ROOT.resolve(),
        Path.cwd().resolve(),
    }
    if resolved in forbidden:
        raise WebAssemblyError("output directory is too broad")
    for source in inputs:
        if (
            resolved == source
            or resolved in source.parents
            or source in resolved.parents
        ):
            raise WebAssemblyError("output directory must be separate from inputs")
    if resolved.exists() and not resolved.is_dir():
        raise WebAssemblyError("output path is not a directory")
    return resolved


def _replace_output(output: Path, staged: Path) -> None:
    if not output.exists():
        staged.replace(output)
        return

    backup = Path(
        tempfile.mkdtemp(prefix=f".{output.name}-backup-", dir=output.parent)
    )
    backup.rmdir()
    output.replace(backup)
    try:
        staged.replace(output)
    except BaseException:
        backup.replace(output)
        raise
    shutil.rmtree(backup)


def _write_json(path: Path, document: object) -> None:
    path.write_bytes(
        json.dumps(document, separators=(",", ":")).encode("utf-8") + b"\n"
    )


def assemble(arguments: argparse.Namespace) -> Path:
    """Validate all inputs, replace the output, and return its resolved path."""

    try:
        version = str(SemVer.parse(arguments.version))
    except VersionError:
        raise WebAssemblyError(
            "version must be canonical SemVer without a leading v"
        ) from None

    release_assets = _required_directory(arguments.release_assets, "Release assets")
    web_root = _required_directory(arguments.web_root, "Web source root")
    vendor_root = _required_directory(arguments.vendor_root, "ESP Web Tools package")

    site_build = _required_directory(
        web_root / "dist", "Astro site build (run npm run build --prefix web)"
    )
    _required_file(site_build / "index.html", "built Web index")
    _required_file(site_build / "devices.json", "device catalog")
    _required_file(site_build / "assets" / "app.js", "built Web application")
    _required_file(site_build / "assets" / "styles.css", "built Web styles")
    vendor_license = _required_file(
        vendor_root / "LICENSE", "ESP Web Tools vendor license"
    )

    _required_file(site_build / "assets" / "flash-engine.js", "bundled flash engine")
    _required_file(site_build / "THIRD-PARTY-NOTICES", "bundled dependency notices")

    enabled = tuple(target for target in TARGETS if target.web_flash is not None)
    if not enabled:
        raise WebAssemblyError("target registry has no Web-enabled targets")

    target_inputs: list[tuple[object, Path, Path, str]] = []
    for target in enabled:
        web = target.web_flash
        if web is None:
            raise AssertionError("enabled target lost Web metadata")
        filename = f"{target.release_stem}-full-v{version}.bin"
        firmware = _required_file(
            release_assets / filename,
            f"{web.name} full firmware",
        )
        art = _required_file(site_build / web.asset, f"{web.name} device art")
        target_inputs.append((target, firmware, art, filename))

    inputs = (
        release_assets,
        web_root,
        vendor_root,
        site_build,
        vendor_license,
        *(item for _, firmware, art, _ in target_inputs for item in (firmware, art)),
    )
    output = _validate_output(arguments.output, inputs)
    output.parent.mkdir(parents=True, exist_ok=True)
    staged = Path(
        tempfile.mkdtemp(prefix=f".{output.name}-", dir=output.parent)
    )

    try:
        shutil.copytree(
            site_build, staged, dirs_exist_ok=True, copy_function=shutil.copyfile
        )
        vendor_output = staged / "vendor" / "esp-web-tools"
        vendor_output.mkdir(parents=True)
        shutil.copyfile(
            site_build / "THIRD-PARTY-NOTICES",
            vendor_output / "THIRD-PARTY-NOTICES",
        )
        shutil.copyfile(vendor_license, vendor_output / "LICENSE")

        firmware_output = staged / "firmware"
        manifest_output = staged / "manifests"
        firmware_output.mkdir()
        manifest_output.mkdir()
        public_targets = []
        for target, firmware, _art, filename in target_inputs:
            web = target.web_flash
            if web is None:
                raise AssertionError("enabled target lost Web metadata")
            shutil.copyfile(firmware, firmware_output / filename)
            # The browser re-downloads this image and has no other way to
            # tell a truncated or tampered response from a good one.
            digest = hashlib.sha256(firmware.read_bytes()).hexdigest()
            manifest_name = f"{target.id}.json"
            _write_json(
                manifest_output / manifest_name,
                {
                    "name": web.name,
                    "version": version,
                    "new_install_prompt_erase": True,
                    "new_install_improv_wait_time": 0,
                    "builds": [
                        {
                            "chipFamily": web.chip_family,
                            "parts": [
                                {
                                    "path": f"../firmware/{filename}",
                                    "offset": 0,
                                    "sha256": digest,
                                }
                            ],
                        }
                    ],
                },
            )
            public_targets.append(
                {
                    "id": target.id,
                    "name": web.name,
                    "description": web.description,
                    "asset": web.asset,
                    "image": filename,
                    "manifest": f"manifests/{manifest_name}",
                }
            )

        _write_json(
            staged / "targets.json",
            {"version": version, "targets": public_targets},
        )
        _replace_output(output, staged)
    except BaseException:
        if staged.exists():
            shutil.rmtree(staged)
        raise
    return output


def main() -> int:
    assemble(_parser().parse_args())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
