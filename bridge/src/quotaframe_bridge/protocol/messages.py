"""Privacy-allowlisted usage.v1 messages and device responses."""

from __future__ import annotations

import json
from dataclasses import dataclass
from datetime import datetime
from typing import Any, Mapping

from quotaframe_bridge.versioning import SemVer, VersionError

from quotaframe_bridge.protocol.validation import validate_name, validate_identifier, validate_token
from quotaframe_bridge.domain.models import Provider, ProviderUsage
from quotaframe_bridge.protocol.ota_messages import OtaMessageError, OtaStatus, parse_ota_status

PROTOCOL_VERSION = 1
MAX_U32 = 0xFFFFFFFF
# The firmware enforces the same bound as `kMaxInverseClockSkewSeconds`
# in usage_core/usage_state.hpp; the two must stay equal.
MAX_INVERSE_CLOCK_SKEW_SECONDS = 300



class ProtocolError(ValueError):
    """A local message or remote response violates usage protocol version 1."""


class AckError(ProtocolError):
    """The device rejected a command or acknowledged a different command."""


class CommandRejected(AckError):
    """A matching response explicitly rejected the command."""


def is_folder_rejection(message: Mapping[str, Any], command: str) -> bool:
    """Folder Push failures report zero or the number of bytes already written."""

    return (
        command in {"char_begin", "file", "chunk", "file_end", "char_end"}
        and message.get("ack") == command
        and message.get("ok") is False
        and type(message.get("n")) is int
        and 0 <= message["n"] <= 0xFFFFFFFF
    )


class IncompatibleProtocolError(ProtocolError):
    """A valid status declares an unsupported base protocol or required capability."""


@dataclass(frozen=True, slots=True)
class DeviceStatus:
    """Validated, non-sensitive device capabilities."""

    name: str
    secure: bool
    protocol: int
    page: str
    capabilities: frozenset[str]
    firmware_version: str = ""
    target: str = ""
    ota: OtaStatus | None = None
    boot_valid: bool = False
    firmware_project: str = ""


class Sequence:
    """Allocate command sequence numbers modulo the unsigned 32-bit range."""

    def __init__(self, initial: int = 0) -> None:
        if isinstance(initial, bool) or not 0 <= initial <= MAX_U32:
            raise ValueError("initial sequence must fit unsigned 32-bit")
        self._value = initial

    def next(self) -> int:
        value = self._value
        self._value = (value + 1) & MAX_U32
        return value


def _u32(value: int, field: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or not 0 <= value <= MAX_U32:
        raise ProtocolError(f"{field} must fit unsigned 32-bit")
    return value


def _u32_text(value: int, field: str) -> str:
    """Encode an exact integer through Buddy's public string getter."""

    return str(_u32(value, field))


def _json_line(payload: Mapping[str, object]) -> bytes:
    return (
        json.dumps(
            payload,
            ensure_ascii=True,
            allow_nan=False,
            separators=(",", ":"),
            sort_keys=True,
        )
        + "\n"
    ).encode("utf-8")


def encode_time_sync(calendar: datetime, seq: int) -> bytes:
    """Encode host-local calendar fields for a time.sync.v1 capable device."""

    if calendar.tzinfo is None or calendar.utcoffset() is None:
        raise ValueError("time sync calendar must be timezone-aware")
    if not 2024 <= calendar.year <= 2099:
        raise ValueError("time sync year must be between 2024 and 2099")
    offset_seconds = calendar.utcoffset().total_seconds()
    if offset_seconds % 60 != 0:
        raise ValueError("time sync UTC offset must use whole minutes")
    offset_minutes = int(offset_seconds // 60)
    if not -840 <= offset_minutes <= 840:
        raise ValueError("time sync UTC offset is outside supported range")

    return _json_line(
        {
            "cmd": "time_sync",
            "v": "1",
            "seq": _u32_text(seq, "seq"),
            "year": str(calendar.year),
            "month": str(calendar.month),
            "day": str(calendar.day),
            "weekday": str((calendar.weekday() + 1) % 7),
            "hour": str(calendar.hour),
            "minute": str(calendar.minute),
            "second": str(calendar.second),
            "utc_offset_min": str(offset_minutes),
        }
    )


def encode_usage(usage: ProviderUsage, seq: int, sent_at: int) -> bytes:
    """Encode only the usage.v1 allowlist."""

    sequence = _u32(seq, "seq")
    sent = _u32(sent_at, "sent_at")
    if sent + MAX_INVERSE_CLOCK_SKEW_SECONDS < usage.sampled_at:
        raise ProtocolError("sent_at precedes sampled_at by more than 300 seconds")

    payload: dict[str, object] = {
        "cmd": "usage",
        "v": _u32_text(PROTOCOL_VERSION, "v"),
        "seq": _u32_text(sequence, "seq"),
        "provider": usage.provider.value,
        "state": usage.state.value,
        "sampled_at": _u32_text(usage.sampled_at, "sampled_at"),
        "sent_at": _u32_text(sent, "sent_at"),
    }
    if usage.short is not None:
        payload["short_used_pct"] = _u32_text(
            usage.short.used_percent, "short_used_pct"
        )
        if usage.short.reset_at is not None:
            payload["short_reset_at"] = _u32_text(
                usage.short.reset_at, "short_reset_at"
            )
    if usage.week is not None:
        payload["week_used_pct"] = _u32_text(
            usage.week.used_percent, "week_used_pct"
        )
        if usage.week.reset_at is not None:
            payload["week_reset_at"] = _u32_text(
                usage.week.reset_at, "week_reset_at"
            )
    return _json_line(payload)


def encode_screen_toggle(seq: int) -> bytes:
    """Request one device-local screensaver state transition."""

    return _json_line({"cmd": "screen_toggle", "v": "1", "seq": _u32_text(seq, "seq")})


def encode_screen_page(seq: int, previous: bool) -> bytes:
    """Request one relative page change, waking the usage view if needed."""

    return _json_line({"cmd": "screen_page", "v": "1", "seq": _u32_text(seq, "seq"),
                       "previous": "1" if previous else "0"})


def encode_status_query() -> bytes:
    """Build the core Buddy status query."""

    return b'{"cmd":"status"}\n'


def parse_ack(
    message: Mapping[str, Any],
    expected_command: str,
    expected_n: int,
) -> None:
    """Require a successful ACK for the command currently in flight."""

    if message.get("ack") != expected_command:
        raise AckError("ack command does not match")
    response_sequence = message.get("n")
    if (
        isinstance(response_sequence, bool)
        or not isinstance(response_sequence, int)
        or (response_sequence != expected_n and not is_folder_rejection(message, expected_command))
    ):
        raise AckError("ack value does not match")
    if message.get("ok") is False:
        error = message.get("error")
        detail = error if isinstance(error, str) and error else "rejected"
        raise CommandRejected(f"device rejected {expected_command}: {detail}")
    if message.get("ok") is not True:
        raise AckError("ack result is invalid")


def parse_status(message: Mapping[str, Any]) -> DeviceStatus:
    """Validate status negotiation before usage publication."""

    if message.get("ack") != "status" or message.get("ok") is not True:
        raise ProtocolError("device did not return successful status")
    data = message.get("data")
    if not isinstance(data, dict):
        raise ProtocolError("status data must be an object")

    name = data.get("name")
    secure = data.get("sec")
    protocol = data.get("protocol")
    page = data.get("page", "")
    caps = data.get("caps")
    try:
        validate_name(name)
    except ValueError as exc:
        raise ProtocolError(str(exc)) from None
    if type(message.get("n")) is not int or message["n"] != 0:
        raise ProtocolError("status ACK counter is invalid")
    if secure is not True:
        raise ProtocolError("device link is not reported secure")
    if (
        isinstance(protocol, bool)
        or not isinstance(protocol, int)
    ):
        raise ProtocolError("device protocol version is invalid")
    try:
        if "page" in data:
            validate_token(page, "page", 31)
        if not isinstance(caps, list) or not 1 <= len(caps) <= 32:
            raise ValueError("device capabilities are invalid")
        for token in caps:
            validate_token(token, "capability")
        capabilities = frozenset(caps)
        if len(capabilities) != len(caps) or sum(map(len, caps)) > 512:
            raise ValueError("device capabilities are duplicated or too large")
    except ValueError as exc:
        raise ProtocolError(str(exc)) from None
    firmware_version = data.get("fw", "")
    target = data.get("target", "")
    firmware_project = data.get("firmware_project", "")
    try:
        if "target" in data:
            validate_identifier(target, "target")
        if "firmware_project" in data:
            validate_identifier(firmware_project, "firmware_project", 64)
        if "fw" in data and (not isinstance(firmware_version, str)
                or not 1 <= len(firmware_version) <= 31
                or any(not 0x20 <= ord(c) <= 0x7E or c in '\"\\' for c in firmware_version)):
            raise ValueError("device firmware version is invalid")
    except ValueError as exc:
        raise ProtocolError(str(exc)) from None
    ota_value = data.get("ota")
    boot_valid = data.get("boot_valid", False)
    if not isinstance(boot_valid, bool):
        raise ProtocolError("device boot validation state is invalid")
    try:
        ota = None if "ota" not in data else parse_ota_status(ota_value)
    except OtaMessageError as exc:
        raise ProtocolError(str(exc)) from None
    if "ota.folder.v1" in capabilities and (
        not firmware_version or not target or not firmware_project or ota is None
    ):
        raise ProtocolError("device OTA status is incomplete")
    if "ota.folder.v1" in capabilities:
        try:
            SemVer.parse(firmware_version.removeprefix("v"))
        except VersionError:
            raise ProtocolError("device OTA firmware version is invalid") from None
    if protocol != PROTOCOL_VERSION:
        raise IncompatibleProtocolError(
            "device protocol is incompatible; update Bridge or firmware, then restart Bridge"
        )
    if "usage.v1" not in capabilities:
        raise IncompatibleProtocolError(
            "device lacks usage.v1; install compatible firmware, then restart Bridge"
        )
    return DeviceStatus(
        name=name,
        secure=True,
        protocol=PROTOCOL_VERSION,
        page=page,
        boot_valid=boot_valid,
        capabilities=capabilities,
        firmware_version=firmware_version,
        target=target,
        firmware_project=firmware_project,
        ota=ota,
    )
