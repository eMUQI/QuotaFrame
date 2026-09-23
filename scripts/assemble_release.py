#!/usr/bin/env python3
"""Assemble the public assets for one product Release.

The requested output directory is treated as disposable build output: after
safety checks reject broad paths and any directory containing an input, all
existing children of that directory are removed before assets are assembled.
"""

from __future__ import annotations

import argparse
import csv
import struct
import hashlib
import json
import shutil
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "bridge" / "src"))

from quotaframe_bridge.release_config import (  # noqa: E402
    ReleaseConfigError,
    validate_repository,
)
from quotaframe_bridge.protocol.ota_messages import (
    encode_ota_manifest,
)
from quotaframe_bridge.targets import TARGETS  # noqa: E402
from quotaframe_bridge.versioning import (  # noqa: E402
    SemVer,
    VersionError,
)


class AssemblyError(ValueError):
    """Release inputs or output location violate the assembly contract."""


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", required=True)
    parser.add_argument("--repository", required=True)
    parser.add_argument(
        "--firmware-root",
        required=True,
        type=Path,
        help="directory containing firmware-<target-id> artifact directories",
    )
    parser.add_argument("--windows-exe", required=True, type=Path)
    parser.add_argument("--windows-setup", required=True, type=Path)
    parser.add_argument("--macos-dmg", required=True, type=Path)
    parser.add_argument("--license", required=True, type=Path)
    parser.add_argument("--third-party-licenses", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    return parser


def _required_file(path: Path, label: str) -> Path:
    resolved = path.expanduser().resolve()
    if not resolved.is_file():
        raise AssemblyError(f"missing required input: {label}")
    return resolved


def _project_version(path: Path, label: str) -> str:
    try:
        document = json.loads(path.read_bytes())
    except (OSError, UnicodeDecodeError, json.JSONDecodeError):
        raise AssemblyError(f"{label} project description is invalid") from None
    if not isinstance(document, dict) or not isinstance(
        document.get("project_version"), str
    ):
        raise AssemblyError(f"{label} project description is invalid")
    return document["project_version"]


def _validate_output(output: Path, inputs: tuple[Path, ...]) -> Path:
    requested = output.expanduser()
    if requested.is_symlink():
        raise AssemblyError("output directory must not be a symbolic link")
    resolved = requested.resolve(strict=False)
    forbidden = {
        Path(resolved.anchor),
        Path.home().resolve(),
        ROOT.resolve(),
        Path.cwd().resolve(),
    }
    if resolved in forbidden:
        raise AssemblyError("output directory is too broad")
    for source in inputs:
        if resolved == source or resolved in source.parents:
            raise AssemblyError("output directory must not contain an input")
    if resolved.exists() and not resolved.is_dir():
        raise AssemblyError("output path is not a directory")
    return resolved


def _prepare_output(output: Path) -> None:
    output.mkdir(parents=True, exist_ok=True)
    for child in output.iterdir():
        if child.is_dir() and not child.is_symlink():
            shutil.rmtree(child)
        else:
            child.unlink()


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _firmware_sources(firmware_root: Path) -> dict[str, dict[str, Path]]:
    root = firmware_root.expanduser().resolve()
    return {
        target.id: {
            "ota": _required_file(
                root / f"firmware-{target.id}" / target.ota_image,
                f"{target.label} OTA firmware image",
            ),
            "full": _required_file(
                root / f"firmware-{target.id}" / target.full_image,
                f"{target.label} full firmware image",
            ),
            "description": _required_file(
                root / f"firmware-{target.id}" / "project_description.json",
                f"{target.label} project description",
            ),
        }
        for target in TARGETS
    }


def _validate_firmware(target, sources: dict[str, Path], version: str) -> None:
    payload = sources["ota"].read_bytes()
    manifest = encode_ota_manifest(target.id, len(payload), _sha256(sources["ota"]), version)
    if len(payload) + len(manifest) > target.ota_transfer_bytes:
        raise AssemblyError(f"{target.label} exceeds the OTA transfer limit")
    project = ROOT / target.firmware_project
    defaults = (project / "sdkconfig.defaults").read_text(encoding="utf-8")
    expected_limit = f"CONFIG_ESP_DESKTOP_BUDDY_FOLDER_PUSH_MAX_TRANSFER_BYTES={target.ota_transfer_bytes}"
    if expected_limit not in defaults.splitlines():
        raise AssemblyError(f"{target.label} OTA transfer limit differs from the release contract")
    rows = csv.reader((project / "partitions.csv").read_text(encoding="utf-8").splitlines())
    slots = {row[0].strip(): (int(row[3].strip(), 0), int(row[4].strip(), 0))
             for row in rows if len(row) >= 5 and row[0].strip() in {"ota_0", "ota_1"}}
    if set(slots) != {"ota_0", "ota_1"} or any(len(payload) > size for _, size in slots.values()):
        raise AssemblyError(f"{target.label} image does not fit both OTA partitions")
    if min(size for _, size in slots.values()) != target.ota_partition_bytes:
        raise AssemblyError(f"{target.label} OTA partition size differs from the registry")
    # ESP image header (24 bytes), first segment header (8), then esp_app_desc_t.
    if (len(payload) < 288 or payload[0] != 0xE9
            or struct.unpack_from("<H", payload, 12)[0] != target.image_chip_id
            or struct.unpack_from("<I", payload, 32)[0] != 0xABCD5432):
        raise AssemblyError(f"{target.label} OTA image header is invalid")
    for offset, expected in ((48, version), (80, Path(target.ota_image).stem)):
        actual = payload[offset:offset + 32].split(b"\0", 1)[0]
        if actual != expected.encode("ascii"):
            raise AssemblyError(f"{target.label} OTA image identity does not match release")
    full = sources["full"].read_bytes()
    app_offset = slots["ota_0"][0]
    if full[app_offset:app_offset + len(payload)] != payload:
        raise AssemblyError(f"{target.label} full image does not contain the same OTA bytes")


def assemble(arguments: argparse.Namespace) -> Path:
    """Validate inputs, replace the output contents, and write exact release assets."""

    try:
        version = str(SemVer.parse(arguments.version))
    except VersionError:
        raise AssemblyError(
            "version must be canonical SemVer without a leading v"
        ) from None
    try:
        repository = validate_repository(arguments.repository)
    except ReleaseConfigError:
        raise AssemblyError("repository must use owner/name form") from None

    firmware = _firmware_sources(arguments.firmware_root)
    common = {
        "windows": _required_file(arguments.windows_exe, "Windows Bridge"),
        "windows_setup": _required_file(arguments.windows_setup, "Windows installer"),
        "macos_dmg": _required_file(arguments.macos_dmg, "macOS Bridge DMG"),
        "license": _required_file(arguments.license, "LICENSE"),
        "notices": _required_file(
            arguments.third_party_licenses,
            "THIRD_PARTY_LICENSES.md",
        ),
    }
    for target in TARGETS:
        built_version = _project_version(
            firmware[target.id]["description"], target.label
        )
        if built_version != version:
            raise AssemblyError(
                f"{target.label} firmware version does not match release version"
            )

        _validate_firmware(target, firmware[target.id], version)

    all_inputs = tuple(common.values()) + tuple(
        path for target_sources in firmware.values() for path in target_sources.values()
    )
    output = _validate_output(arguments.output, all_inputs)
    _prepare_output(output)

    names = {
        "windows": f"quotaframe-bridge-windows-v{version}.exe",
        "windows_setup": f"quotaframe-bridge-windows-v{version}-setup.exe",
        "macos_dmg": f"quotaframe-bridge-macos-arm64-v{version}.dmg",
    }
    for key, filename in names.items():
        shutil.copyfile(common[key], output / filename)
    shutil.copyfile(common["license"], output / "LICENSE")
    shutil.copyfile(common["notices"], output / "THIRD_PARTY_LICENSES.md")

    images = []
    firmware_names: list[str] = []
    for target in TARGETS:
        ota_name = f"{target.release_stem}-ota-v{version}.bin"
        full_name = f"{target.release_stem}-full-v{version}.bin"
        shutil.copyfile(firmware[target.id]["ota"], output / ota_name)
        shutil.copyfile(firmware[target.id]["full"], output / full_name)
        firmware_names.extend((ota_name, full_name))
        ota_path = output / ota_name
        images.append(
            {
                "target": target.id,
                "firmware_project": "quotaframe",
                "kind": "app",
                "version": version,
                "size": ota_path.stat().st_size,
                "sha256": _sha256(ota_path),
                "url": (
                    f"https://github.com/{repository}/releases/download/"
                    f"v{version}/{ota_name}"
                ),
            }
        )

    manifest = {"schema_version": 1, "releases": images}
    (output / "manifest.json").write_bytes(
        json.dumps(manifest, separators=(",", ":")).encode("utf-8") + b"\n"
    )

    checksum_names = sorted((*names.values(), *firmware_names, "manifest.json", "LICENSE", "THIRD_PARTY_LICENSES.md"))
    checksum_lines = [
        f"{_sha256(output / filename)}  {filename}" for filename in checksum_names
    ]
    (output / "SHA256SUMS.txt").write_text(
        "\n".join(checksum_lines) + "\n",
        encoding="ascii",
        newline="\n",
    )
    return output


def main(argv: list[str] | None = None) -> int:
    parser = _parser()
    arguments = parser.parse_args(argv)
    try:
        output = assemble(arguments)
    except AssemblyError as exc:
        parser.error(str(exc))
    print(f"Release assets assembled at {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
