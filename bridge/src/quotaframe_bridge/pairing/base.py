"""Transport-independent pairing interface."""

from __future__ import annotations

from typing import Any, Protocol


class PairingError(ConnectionError):
    """A sanitized pairing failure, with no platform or user detail."""


class PairingBusyError(PairingError):
    """The platform already owns an unfinished pairing or unpairing operation."""


class PairingGuidanceError(PairingError):
    """A pairing failure whose message is a fixed instruction for the user.

    Every other `PairingError` is collapsed to one constant string before it
    reaches the user, because its message may have been derived from platform
    state. Messages carried by this subclass are literals owned by the Bridge,
    so the transport may surface them verbatim.
    """


class DevicePairer(Protocol):
    """Ensure a scanned BLE device has the required authenticated bond."""

    async def ensure_paired(self, device: Any) -> None:
        """Prepare the device for a secure GATT connection."""
