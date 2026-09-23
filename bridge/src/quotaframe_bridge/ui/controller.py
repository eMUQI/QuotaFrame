"""Turn panel snapshots into tray calls, with no toolkit knowledge."""

from __future__ import annotations

import logging
from collections.abc import Callable

from quotaframe_bridge.ui.notifications import (
    NotificationKind,
    NotificationPolicy,
)
from quotaframe_bridge.ui.shell import TrayShell
from quotaframe_bridge.ui.status import (
    PanelStatus,
    format_info_lines,
    format_tooltip,
)

LOGGER = logging.getLogger(__name__)


class TrayController:
    """The only component that both reads status and drives the shell."""

    def __init__(self, shell: TrayShell, policy: NotificationPolicy) -> None:
        self._shell = shell
        self._policy = policy

    def apply(self, status: PanelStatus, now: float) -> None:
        """Publish one snapshot to the tray and raise anything it earns.

        Each stage is isolated so one formatting failure cannot take the icon,
        the tooltip and both menu rows down together, leaving the tray frozen
        on its startup placeholder with nothing on screen to say why.
        """

        self._stage("state", lambda: self._shell.set_state(status.state))
        self._stage(
            "info lines",
            lambda: self._shell.set_info_lines(format_info_lines(status)),
        )
        self._stage(
            "tooltip",
            lambda: self._shell.set_tooltip(format_tooltip(status)),
        )
        self._stage("notifications", lambda: self._raise(status, now))

    def _raise(self, status: PanelStatus, now: float) -> None:
        for notification in self._policy.evaluate(status, now):
            self._shell.notify(notification.title, notification.body)

    def _stage(self, name: str, update: Callable[[], None]) -> None:
        try:
            update()
        except Exception:
            LOGGER.exception("tray %s update failed", name)

    def announce(
        self,
        kind: NotificationKind,
        now: float,
        *,
        device: str = "",
    ) -> None:
        """Raise a notification that no snapshot could have expressed."""

        for notification in self._policy.emit(kind, now, device=device):
            self._shell.notify(notification.title, notification.body)

    def set_autostart_checked(self, checked: bool) -> None:
        self._shell.set_autostart_checked(checked)

    def set_firmware_action(
        self, label: str, enabled: bool, detail: str
    ) -> None:
        self._shell.set_firmware_action(label, enabled, detail)

    def set_bridge_update_action(self, enabled: bool, detail: str) -> None:
        self._shell.set_bridge_update_action(enabled, detail)

    def notify(self, title: str, body: str) -> None:
        self._shell.notify(title, body)

    def stop(self) -> None:
        self._shell.stop()
