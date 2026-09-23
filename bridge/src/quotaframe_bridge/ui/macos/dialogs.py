"""Native macOS dialogs for device management and Bridge startup.

macOS owns the Bluetooth pairing prompt. Application dialogs provide manual
recovery instructions and identify an already running Bridge instance.
"""

from __future__ import annotations

import logging
import subprocess
from collections.abc import Callable
from dataclasses import dataclass

from quotaframe_bridge.i18n import tr

LOGGER = logging.getLogger(__name__)

REPAIR_TITLE = tr("repair_panel")
# A dropped link is only noticed by the next send, which sits behind a
# collection cycle, so reconnection cannot be promised as prompt.
REPAIR_BODY = tr("repair_macos_body")
REPAIR_CONFIRM = tr("open_bluetooth")
REPAIR_CANCEL = tr("cancel")

FORGET_TITLE = tr("forget")
FORGET_BODY_PREFIX = tr("forget_macos_body")
FORGET_CANCEL = tr("cancel")

ALREADY_RUNNING_TITLE = tr("already_running")
ALREADY_RUNNING_BODY = tr("already_running_macos")
ALREADY_RUNNING_CONFIRM = tr("ok")

BLUETOOTH_SETTINGS_URL = "x-apple.systempreferences:com.apple.Bluetooth-Settings.extension"


@dataclass(frozen=True)
class AlertBindings:
    """The AppKit calls the dialogs make, late-bound for testability."""

    run_alert: Callable[[str, str, tuple[str, ...]], int]
    open_url: Callable[[str], None]


def load_alert_bindings() -> AlertBindings:
    try:
        import AppKit
    except ImportError as exc:  # pragma: no cover - macOS only
        raise RuntimeError("NSAlert requires pyobjc") from exc

    def run_alert(title: str, body: str, buttons: tuple[str, ...]) -> int:
        alert = AppKit.NSAlert.alloc().init()
        alert.setMessageText_(title)
        alert.setInformativeText_(body)
        for button in buttons:
            alert.addButtonWithTitle_(button)
        # NSAlertFirstButtonReturn is 1000; return the index of the button.
        return int(alert.runModal()) - 1000

    def open_url(url: str) -> None:
        AppKit.NSWorkspace.sharedWorkspace().openURL_(
            AppKit.NSURL.URLWithString_(url)
        )

    return AlertBindings(run_alert=run_alert, open_url=open_url)


class MacDialogs:
    """The macOS half of `PinPromptUI`."""

    def __init__(self, bindings: AlertBindings | None = None) -> None:
        self._bindings = load_alert_bindings() if bindings is None else bindings

    async def ask_pin(self, device_name: str) -> str:
        """Never called on macOS.

        The system runs the passkey ceremony itself when the Bridge first
        touches an encrypted characteristic, so nothing in this process ever
        sees or asks for the six digits. Raising is better than returning a
        placeholder that would be sent somewhere as if it were a real code.
        """

        raise NotImplementedError(
            "macOS collects the pairing code in its own dialog"
        )

    def show_repair_guidance(self) -> bool:
        """Explain what the user has to do, and offer to open Settings.

        Synchronous, because the menu handler that calls it is already on the
        main thread and an NSAlert is modal there.
        """

        choice = self._bindings.run_alert(
            REPAIR_TITLE,
            REPAIR_BODY,
            (REPAIR_CONFIRM, REPAIR_CANCEL),
        )
        if choice == 0:
            self.open_bluetooth_settings()
        return False

    async def confirm_repair(self, device_name: str) -> bool:
        """The `PinPromptUI` spelling of `show_repair_guidance`.

        Returns False either way: nothing was repaired, because nothing in this
        process can repair it. The return value exists to satisfy the protocol,
        where on Windows True means "go ahead and re-pair now".
        """

        return self.show_repair_guidance()

    def confirm_firmware(self, body: str) -> bool:
        return self._bindings.run_alert(tr("check_and_update"), body, (tr("ok"), tr("cancel"))) == 0

    def choose_device_to_forget(self, labels: tuple[str, ...]) -> int | None:
        """Ask which owned board to drop, returning its position or None.

        One button per device plus Cancel. macOS has no list-selection alert
        without a custom accessory view, and the owned set is small enough
        that buttons stay readable.
        """

        if not labels:
            return None
        buttons = (*labels, FORGET_CANCEL)
        choice = self._bindings.run_alert(
            FORGET_TITLE,
            FORGET_BODY_PREFIX,
            buttons,
        )
        if choice < 0 or choice >= len(labels):
            return None
        return choice

    def open_bluetooth_settings(self) -> None:
        try:
            self._bindings.open_url(BLUETOOTH_SETTINGS_URL)
        except Exception:
            LOGGER.warning("could not open Bluetooth settings")

    def show_already_running(self) -> None:
        self._bindings.run_alert(
            ALREADY_RUNNING_TITLE,
            ALREADY_RUNNING_BODY,
            (ALREADY_RUNNING_CONFIRM,),
        )

    def close_dialogs(self) -> None:
        """Nothing to do: every alert here is modal and already closed."""


def open_log_directory(path, *, runner=subprocess.run) -> None:
    """Reveal the rotating log in Finder."""

    runner(["/usr/bin/open", str(path)], check=False)
