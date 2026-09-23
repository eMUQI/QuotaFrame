"""Ordered transport contract for Bridge device sessions."""

from __future__ import annotations

from enum import Enum
from typing import Any, Protocol


class WritePolicy(Enum):
    NORMAL = "normal"
    FOLDER_PUSH = "folder_push"


class TransportReset(ConnectionError):
    """The link was lost; callers must reconnect and use a new sequence."""


class TransportCleanupError(RuntimeError):
    """The old BLE client could not be released; recovery requires a restart."""


class TransportConnectionError(ConnectionError):
    """A sanitized connection error suitable for user-facing retry logs."""


class UsageTransport(Protocol):
    """Ordered command transport with application-level acknowledgement."""

    async def connect(self) -> None:
        """Prepare the transport and negotiate device compatibility."""

    async def send_command(
        self,
        payload: bytes,
        command: str,
        expected_ack_n: int,
        write_policy: WritePolicy = WritePolicy.NORMAL,
    ) -> None:
        """Send one newline-delimited command and await its matching ACK."""

    async def close(self) -> None:
        """Release transport resources."""


def device_status(transport: Any) -> Any | None:
    """Return the negotiated `DeviceStatus`, whichever layer holds it.

    A reconnecting `DeviceManager` republishes it as `device_status`, while a
    bare `BleakNusTransport` keeps it as `status`. Callers that only want to
    know what the device can do should not have to care which one they hold,
    and a console or mock transport that has neither answers None.
    """

    for attribute in ("device_status", "status"):
        status = getattr(transport, attribute, None)
        if status is not None:
            return status
    return None
