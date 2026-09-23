"""Privacy-allowlisted Folder Push OTA messages and status fields."""

from __future__ import annotations

import base64
import binascii
import json
import re
from dataclasses import dataclass
from typing import Any, Mapping

from quotaframe_bridge.versioning import SemVer, VersionError

PROTOCOL_VERSION = 1
MAX_U32 = 0xFFFFFFFF
FOLDER_PUSH_BLOCK_BYTES = 2880
FOLDER_PUSH_TRANSFER_NAME = "firmware"
FOLDER_PUSH_MANIFEST_PATH = "manifest.json"
FOLDER_PUSH_FIRMWARE_PATH = "firmware.bin"
VALID_PHASES = frozenset(
    {"idle", "confirming", "receiving", "verifying", "rebooting"}
)
VALID_ERRORS = frozenset(
    {"", "denied", "timeout", "too_large", "bad_image", "link_lost", "seq_gap", "low_power", "project_mismatch", "target_mismatch"}
)
_LOWER_SHA256 = re.compile(r"^[0-9a-f]{64}$")


class OtaMessageError(ValueError):
    """An OTA wire value is malformed or outside its public bounds."""


@dataclass(frozen=True, slots=True)
class OtaStatus:
    """Validated public OTA state returned by the device status endpoint."""

    phase: str
    offset: int
    size: int
    error: str


def _u32(value: int, field: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or not 0 <= value <= MAX_U32:
        raise OtaMessageError(f"{field} must fit unsigned 32-bit")
    return value


def parse_decimal_text(value: object, field: str) -> int:
    """Decode the protocol's canonical unsigned decimal text representation."""

    if not isinstance(value, str) or not value or not value.isascii():
        raise OtaMessageError(f"{field} must be a canonical decimal string")
    if value != "0" and value.startswith("0"):
        raise OtaMessageError(f"{field} must be a canonical decimal string")
    if not value.isdecimal():
        raise OtaMessageError(f"{field} must be a canonical decimal string")
    return _u32(int(value), field)


def _version(value: str) -> str:
    if not isinstance(value, str) or not 1 <= len(value) <= 31:
        raise OtaMessageError("version is invalid")
    try:
        SemVer.parse(value)
    except VersionError:
        raise OtaMessageError("version is invalid") from None
    return value


def _digest(value: str) -> str:
    if not isinstance(value, str) or _LOWER_SHA256.fullmatch(value) is None:
        raise OtaMessageError("sha256 must be 64 lowercase hexadecimal characters")
    return value


def _json_line(payload: Mapping[str, object]) -> bytes:
    return (
        json.dumps(payload, ensure_ascii=True, allow_nan=False, separators=(",", ":"), sort_keys=True)
        + "\n"
    ).encode("utf-8")


def encode_ota_manifest(
    target: str, size: int, sha256: str, version: str, firmware_project: str = "quotaframe"
) -> bytes:
    """Encode the exact six-field manifest accepted by the firmware sink."""

    from quotaframe_bridge.protocol.validation import validate_identifier
    validate_identifier(firmware_project, "firmware_project", 64)
    validate_identifier(target, "target")
    declared_size = _u32(size, "size")
    if declared_size == 0:
        raise OtaMessageError("size must be positive")
    return json.dumps(
        {
            "schema": "1",
            "firmware_project": firmware_project,
            "target": target,
            "version": _version(version),
            "size": declared_size,
            "sha256": _digest(sha256),
        },
        ensure_ascii=True,
        allow_nan=False,
        separators=(",", ":"),
        sort_keys=True,
    ).encode("utf-8")


def encode_folder_push_begin(total: int) -> bytes:
    """Begin the single allowlisted firmware transfer with its decoded byte total."""

    declared_total = _u32(total, "total")
    if declared_total == 0:
        raise OtaMessageError("total must be positive")
    return _json_line(
        {
            "cmd": "char_begin",
            "name": FOLDER_PUSH_TRANSFER_NAME,
            "total": declared_total,
        }
    )


def encode_folder_push_file(path: str, size: int) -> bytes:
    """Begin the manifest or firmware file; no other paths are accepted."""

    if path not in {FOLDER_PUSH_MANIFEST_PATH, FOLDER_PUSH_FIRMWARE_PATH}:
        raise OtaMessageError("folder push path is not allowlisted")
    declared_size = _u32(size, "size")
    if declared_size == 0:
        raise OtaMessageError("file size must be positive")
    return _json_line({"cmd": "file", "path": path, "size": declared_size})


def encode_folder_push_chunk(data: bytes) -> bytes:
    """Encode one bounded decoded block as the protocol's base64 chunk command."""

    if not isinstance(data, bytes) or not 1 <= len(data) <= FOLDER_PUSH_BLOCK_BYTES:
        raise OtaMessageError("Folder Push block must contain 1 to 2880 bytes")
    return _json_line(
        {"cmd": "chunk", "d": base64.b64encode(data).decode("ascii")}
    )


def decode_folder_push_chunk(message: Mapping[str, Any]) -> bytes:
    """Decode and bound-check one Folder Push base64 chunk."""

    encoded = message.get("d")
    if not isinstance(encoded, str) or not encoded.isascii():
        raise OtaMessageError("d must be ASCII text")
    try:
        data = base64.b64decode(encoded, validate=True)
    except (binascii.Error, ValueError) as exc:
        raise OtaMessageError("d is invalid base64") from exc
    if not 1 <= len(data) <= FOLDER_PUSH_BLOCK_BYTES:
        raise OtaMessageError("decoded Folder Push block is outside bounds")
    return data


def _simple_command(command: str, sequence: int) -> bytes:
    return _json_line(
        {"cmd": command, "v": str(PROTOCOL_VERSION), "seq": str(_u32(sequence, "seq"))}
    )


def encode_folder_push_file_end() -> bytes:
    """End the currently active Folder Push file."""

    return _json_line({"cmd": "file_end"})


def encode_folder_push_end() -> bytes:
    """End the firmware transfer after both allowlisted files completed."""

    return _json_line({"cmd": "char_end"})


def encode_ota_abort(sequence: int) -> bytes:
    """Encode the versioned OTA abort command used to reset stale device state."""

    return _simple_command("ota_abort", sequence)


def parse_ota_status(value: object) -> OtaStatus:
    """Validate the public OTA status object and its phase/offset invariants."""

    if not isinstance(value, dict):
        raise OtaMessageError("ota status must be an object")
    phase = value.get("phase")
    error = value.get("err")
    if phase not in VALID_PHASES:
        raise OtaMessageError("ota phase is invalid")
    if error not in VALID_ERRORS:
        raise OtaMessageError("ota error is invalid")
    offset = parse_decimal_text(value.get("off"), "ota.off")
    size = parse_decimal_text(value.get("size"), "ota.size")
    if offset > size:
        raise OtaMessageError("ota offset exceeds declared size")
    if phase == "idle" and (offset != 0 or size != 0):
        raise OtaMessageError("idle OTA status must have zero offset and size")
    return OtaStatus(phase=phase, offset=offset, size=size, error=error)
