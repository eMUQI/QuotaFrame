"""Shared validation for public device identity and persisted metadata."""

import re

_TOKEN = re.compile(r"[A-Za-z0-9._-]+")
_ID = re.compile(r"[a-z0-9][a-z0-9._-]*")
_BIDI = {0x061C, 0x200E, 0x200F, *range(0x202A, 0x202F), *range(0x2066, 0x206A)}


def validate_name(value: object) -> str:
    if not isinstance(value, str) or not 1 <= len(value) <= 64 or value != value.strip():
        raise ValueError("device name is invalid")
    if any(ord(c) < 32 or 0x7F <= ord(c) <= 0x9F or ord(c) in _BIDI
           or ord(c) in {0x2028, 0x2029} or 0xD800 <= ord(c) <= 0xDFFF for c in value):
        raise ValueError("device name contains prohibited characters")
    if len(value.encode("utf-8")) > 128:
        raise ValueError("device name exceeds 128 UTF-8 bytes")
    return value


def validate_identifier(value: object, field: str, maximum: int = 31) -> str:
    if not isinstance(value, str) or not 1 <= len(value) <= maximum or not _ID.fullmatch(value):
        raise ValueError(f"{field} is invalid")
    return value


def validate_token(value: object, field: str, maximum: int = 64) -> str:
    if not isinstance(value, str) or not 1 <= len(value) <= maximum or not _TOKEN.fullmatch(value):
        raise ValueError(f"{field} is invalid")
    return value


def unique_object(pairs: list[tuple[str, object]]) -> dict[str, object]:
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("duplicate JSON object key")
        key.encode("utf-8", errors="strict")
        pending = [value]
        while pending:
            item = pending.pop()
            if isinstance(item, str):
                item.encode("utf-8", errors="strict")
            elif isinstance(item, list):
                pending.extend(item)
        result[key] = value
    return result
