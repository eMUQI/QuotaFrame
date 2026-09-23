"""Incremental newline-delimited JSON decoder."""

from __future__ import annotations

import json
from typing import Any

from quotaframe_bridge.protocol.validation import unique_object


class LineDecodeError(ValueError):
    """A notification stream violated the bounded JSONL contract."""


def _reject_constant(value: str) -> None:
    raise LineDecodeError(f"non-finite number is not supported: {value}")


def _unsigned_json_integer(value: str) -> int:
    if value.startswith("-"):
        raise LineDecodeError("negative JSON integer is not supported")
    return int(value)


class LineDecoder:
    """Buffer fragmented BLE notifications and return complete JSON objects."""

    def __init__(self, max_line_bytes: int) -> None:
        if max_line_bytes < 2:
            raise ValueError("max_line_bytes must be at least 2")
        self._maximum = max_line_bytes
        self._buffer = bytearray()

    def feed(self, data: bytes) -> list[dict[str, Any]]:
        """Decode zero or more objects and retain the incomplete tail."""

        self._buffer.extend(data)
        messages: list[dict[str, Any]] = []
        try:
            while True:
                newline = self._buffer.find(b"\n")
                if newline < 0:
                    if len(self._buffer) > self._maximum + (1 if self._buffer.endswith(b"\r") else 0):
                        raise LineDecodeError("unterminated line exceeds maximum size")
                    break

                line = bytes(self._buffer[:newline])
                del self._buffer[: newline + 1]
                if line.endswith(b"\r"):
                    line = line[:-1]
                if not line:
                    continue
                if len(line) > self._maximum:
                    raise LineDecodeError("line exceeds maximum size")
                try:
                    decoded = json.loads(
                        line.decode("utf-8", errors="strict"),
                        parse_constant=_reject_constant,
                        parse_int=_unsigned_json_integer,
                        object_pairs_hook=unique_object,
                    )
                except (UnicodeError, ValueError, RecursionError) as exc:
                    raise LineDecodeError("line is not a valid UTF-8 JSON object") from exc
                if not isinstance(decoded, dict):
                    raise LineDecodeError("line JSON must be an object")
                messages.append(decoded)
        except LineDecodeError:
            self._buffer.clear()
            raise
        return messages
