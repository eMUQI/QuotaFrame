"""macOS Bluetooth authorization preflight.

macOS gates CoreBluetooth behind TCC. A process that touches it without an
authorized `NSBluetoothAlwaysUsageDescription` is not refused — it is killed
with SIGABRT, printing nothing to stdout or stderr, which from the user's side
looks exactly like the Bridge silently disappearing. The reason lands only in a
crash report:

    "termination": {"namespace": "TCC", "details": ["This app has crashed
     because it attempted to access privacy-sensitive data without a usage
     description..."]}

`CBManager.authorization` can be read without starting a manager and without
tripping that gate, so checking it first turns the two recoverable cases into
messages the user can act on. It cannot rule out the third: an unbundled
process whose responsible parent has no usage description still dies at the
first scan, which is why the not-determined case says so out loud.
"""

from __future__ import annotations

import logging
from collections.abc import Callable

logger = logging.getLogger(__name__)

NOT_DETERMINED = 0
RESTRICTED = 1
DENIED = 2
ALLOWED = 3

DENIED_INSTRUCTIONS = (
    "macOS is blocking Bluetooth for this process; open System Settings > "
    "Privacy & Security > Bluetooth, enable the terminal or application that "
    "launches the Bridge, then run it again"
)
FIRST_RUN_NOTICE = (
    "macOS has not yet been asked for Bluetooth access. It should prompt "
    "during the first scan. If the Bridge instead exits immediately with no "
    "message, macOS killed it for having no Bluetooth usage description: "
    "start it from Terminal.app or another application that already holds "
    "Bluetooth access"
)

AuthorizationReader = Callable[[], "int | None"]


class BluetoothPermissionError(RuntimeError):
    """macOS has refused this process access to Bluetooth."""


def read_authorization() -> int | None:
    """Return the CoreBluetooth authorization state, or None when unknown."""

    try:
        import CoreBluetooth
    except ImportError:
        return None
    try:
        return int(CoreBluetooth.CBManager.authorization())
    except (AttributeError, TypeError, ValueError, OSError):
        return None


def preflight(
    *,
    reader: AuthorizationReader = read_authorization,
    log: logging.Logger | None = None,
) -> None:
    """Fail loudly on a denied gate; warn before an unattributable one."""

    state = reader()
    if state in (DENIED, RESTRICTED):
        raise BluetoothPermissionError(DENIED_INSTRUCTIONS)
    if state == NOT_DETERMINED:
        (log or logger).info("%s", FIRST_RUN_NOTICE)
