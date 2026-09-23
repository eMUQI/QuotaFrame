"""When the tray is allowed to raise a balloon, and what it says."""

from __future__ import annotations

import enum
from dataclasses import dataclass

from quotaframe_bridge.i18n import tr
from quotaframe_bridge.ui.status import PanelStatus

DEVICE_LOST_AFTER_S = 90.0
MIN_INTERVAL_PER_KIND_S = 600.0


class NotificationKind(enum.Enum):
    DEVICE_LOST = "device_lost"
    DEVICE_RESTORED = "device_restored"
    PAIRING_REQUIRED = "pairing_required"
    STARTUP_FAILED = "startup_failed"


# Notification text is maintained in quotaframe_bridge.catalog.
COPY: dict[NotificationKind, tuple[str, str]] = {
    NotificationKind.DEVICE_LOST: (
        tr("panel_disconnected"),
        tr("panel_disconnected_body"),
    ),
    NotificationKind.DEVICE_RESTORED: (tr("panel_restored"), tr("panel_restored_body")),
    NotificationKind.PAIRING_REQUIRED: (
        tr("pairing_required"),
        tr("pairing_required_body"),
    ),
    NotificationKind.STARTUP_FAILED: (tr("startup_failed"), tr("startup_failed_body")),
}


@dataclass(frozen=True, slots=True)
class Notification:
    """One balloon, already rendered."""

    kind: NotificationKind
    title: str
    body: str


class NotificationPolicy:
    """Decide which balloons may fire, and rate limit every one of them."""

    def __init__(self) -> None:
        self._disconnected_since: dict[str, float] = {}
        self._loss_reported: set[str] = set()
        self._last_sent: dict[NotificationKind, float] = {}

    def emit(
        self,
        kind: NotificationKind,
        now: float,
        *,
        device: str = "",
    ) -> tuple[Notification, ...]:
        """Raise one notification directly, subject to rate limiting."""

        if not self._allow(kind, now):
            return ()
        title, body = COPY[kind]
        return (Notification(kind, title, body.format(device=device)),)

    def evaluate(
        self,
        current: PanelStatus,
        now: float,
    ) -> tuple[Notification, ...]:
        """Return the notifications this status change has earned."""

        return tuple(self._evaluate_devices(current, now))

    def _evaluate_devices(
        self,
        current: PanelStatus,
        now: float,
    ) -> list[Notification]:
        fired: list[Notification] = []
        for device in current.devices:
            if device.incompatible:
                self._disconnected_since.pop(device.label, None)
                continue
            # A board that has never connected is not a board that dropped.
            if not device.ever_connected:
                continue
            if device.connected:
                self._disconnected_since.pop(device.label, None)
                if device.label in self._loss_reported:
                    self._loss_reported.discard(device.label)
                    fired.extend(
                        self.emit(
                            NotificationKind.DEVICE_RESTORED,
                            now,
                            device=device.label,
                        )
                    )
                continue
            since = self._disconnected_since.setdefault(device.label, now)
            if device.label in self._loss_reported:
                continue
            if now - since < DEVICE_LOST_AFTER_S:
                continue
            self._loss_reported.add(device.label)
            fired.extend(
                self.emit(
                    NotificationKind.DEVICE_LOST,
                    now,
                    device=device.label,
                )
            )
        return fired

    def _allow(self, kind: NotificationKind, now: float) -> bool:
        previous = self._last_sent.get(kind)
        if previous is not None and now - previous < MIN_INTERVAL_PER_KIND_S:
            return False
        self._last_sent[kind] = now
        return True
