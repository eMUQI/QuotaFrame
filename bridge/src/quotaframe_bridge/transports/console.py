"""BLE-free console transport used only with mock data."""

from __future__ import annotations

import json
import sys
from typing import TextIO

from quotaframe_bridge.protocol.messages import parse_ack
from quotaframe_bridge.transports.base import WritePolicy


class ConsoleTransport:
    """Print exactly the JSON lines that BLE would carry."""

    def __init__(self, output: TextIO | None = None) -> None:
        self._output = sys.stdout if output is None else output
        self._connected = False

    async def connect(self) -> None:
        self._connected = True

    async def send_command(
        self,
        payload: bytes,
        command: str,
        expected_ack_n: int,
        write_policy: WritePolicy = WritePolicy.NORMAL,
    ) -> None:
        if not self._connected:
            raise RuntimeError("console transport is not connected")
        decoded = payload.decode("utf-8", errors="strict")
        parsed = json.loads(decoded)
        if not isinstance(parsed, dict):
            raise RuntimeError("console payload is not a JSON object")
        self._output.write(decoded)
        self._output.flush()
        parse_ack(
            {"ack": command, "ok": True, "n": expected_ack_n},
            command,
            expected_ack_n,
        )

    async def close(self) -> None:
        self._connected = False
