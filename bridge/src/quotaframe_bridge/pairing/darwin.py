"""macOS pairing, which macOS insists on driving itself.

CoreBluetooth exposes no pairing API: there is no counterpart to the WinRT
`PROVIDE_PIN` ceremony that `pairing.windows` performs before opening GATT.
A central pairs implicitly, when it first touches a characteristic the
peripheral protects — which is exactly what the firmware does with its
encrypted Nordic UART Service. macOS then shows its own six-digit dialog and
stores the bond in the system Bluetooth database.

So this pairer performs no ceremony. It exists to keep the transport's
`DevicePairer` seam symmetric, to log what the user is about to see, and to
give `--repair-pairing` an honest answer instead of silently doing nothing:
removing a macOS bond needs Bluetooth settings, not an API call.
"""

from __future__ import annotations

import logging
from typing import Any

from quotaframe_bridge.pairing.base import PairingGuidanceError

logger = logging.getLogger(__name__)

REPAIR_INSTRUCTIONS = (
    "macOS cannot remove a bond programmatically; open System Settings > "
    "Bluetooth, use the (i) next to the panel and Forget This Device, then "
    "run the Bridge again"
)


class DarwinPairer:
    """Let macOS run the passkey ceremony when the encrypted GATT read starts."""

    def __init__(self, *, repair: bool = False) -> None:
        self._repair = repair
        self._announced = False

    async def ensure_paired(self, device: Any) -> None:
        if self._repair:
            raise PairingGuidanceError(REPAIR_INSTRUCTIONS)
        if not self._announced:
            self._announced = True
            logger.info(
                "macOS drives pairing itself: accept the system dialog and "
                "enter the six-digit code shown on the panel"
            )
