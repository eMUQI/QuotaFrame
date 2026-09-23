from __future__ import annotations

import unittest
from dataclasses import replace

from quotaframe_bridge.i18n import tr
from quotaframe_bridge.domain.models import Provider, ProviderUsage, SourceState
from quotaframe_bridge.ui.notifications import (
    DEVICE_LOST_AFTER_S,
    MIN_INTERVAL_PER_KIND_S,
    NotificationKind,
    NotificationPolicy,
)
from quotaframe_bridge.ui.status import DeviceStatus, PanelStatus


def unavailable(provider: Provider) -> ProviderUsage:
    return ProviderUsage(
        provider=provider,
        state=SourceState.UNAVAILABLE,
        sampled_at=1,
    )


def status(*, connected: bool, ever: bool = True, failures: int = 0) -> PanelStatus:
    return PanelStatus(
        devices=(DeviceStatus("M5", connected=connected, ever_connected=ever),),
        providers={
            Provider.CODEX: unavailable(Provider.CODEX),
            Provider.CLAUDE: unavailable(Provider.CLAUDE),
        },
        consecutive_failures=failures,
        resolution_error=None,
    )


class DeviceNotificationTests(unittest.TestCase):
    def test_incompatible_device_pauses_loss_notification_timer(self) -> None:
        policy = NotificationPolicy()
        disconnected = status(connected=False)
        incompatible = replace(
            disconnected,
            devices=(replace(disconnected.devices[0], incompatible=True),),
        )
        policy.evaluate(status(connected=True), 0.0)
        policy.evaluate(disconnected, 10.0)
        self.assertEqual(policy.evaluate(incompatible, 20.0), ())
        self.assertEqual(policy.evaluate(incompatible, 200.0), ())
        self.assertEqual(policy.evaluate(disconnected, 300.0), ())
        self.assertEqual(
            policy.evaluate(disconnected, 300.0 + DEVICE_LOST_AFTER_S - 1), (),
        )
        fired = policy.evaluate(disconnected, 300.0 + DEVICE_LOST_AFTER_S)
        self.assertEqual(
            tuple(item.kind for item in fired), (NotificationKind.DEVICE_LOST,),
        )
        restored = policy.evaluate(status(connected=True), 400.0)
        self.assertEqual(
            tuple(item.kind for item in restored), (NotificationKind.DEVICE_RESTORED,),
        )

    def test_short_drop_does_not_notify(self) -> None:
        policy = NotificationPolicy()
        policy.evaluate(status(connected=True), 0.0)
        fired = policy.evaluate(status(connected=False), 10.0)
        self.assertEqual(fired, ())

    def test_sustained_drop_notifies_once(self) -> None:
        policy = NotificationPolicy()
        policy.evaluate(status(connected=True), 0.0)
        policy.evaluate(status(connected=False), 10.0)
        first = policy.evaluate(status(connected=False), 10.0 + DEVICE_LOST_AFTER_S)
        second = policy.evaluate(status(connected=False), 500.0 + DEVICE_LOST_AFTER_S)
        self.assertEqual(
            tuple(item.kind for item in first),
            (NotificationKind.DEVICE_LOST,),
        )
        self.assertEqual(second, ())

    def test_recovery_notifies_and_rearms(self) -> None:
        policy = NotificationPolicy()
        policy.evaluate(status(connected=True), 0.0)
        policy.evaluate(status(connected=False), 10.0)
        policy.evaluate(status(connected=False), 10.0 + DEVICE_LOST_AFTER_S)
        restored = policy.evaluate(status(connected=True), 900.0)
        self.assertEqual(
            tuple(item.kind for item in restored),
            (NotificationKind.DEVICE_RESTORED,),
        )

        policy.evaluate(status(connected=False), 1000.0)
        again = policy.evaluate(status(connected=False), 1000.0 + DEVICE_LOST_AFTER_S)
        self.assertEqual(
            tuple(item.kind for item in again),
            (NotificationKind.DEVICE_LOST,),
        )

    def test_never_connected_device_never_notifies(self) -> None:
        policy = NotificationPolicy()
        policy.evaluate(status(connected=False, ever=False), 0.0)
        fired = policy.evaluate(
            status(connected=False, ever=False),
            10_000.0,
        )
        self.assertEqual(fired, ())

    def test_recovery_without_a_prior_loss_is_silent(self) -> None:
        policy = NotificationPolicy()
        policy.evaluate(status(connected=True), 0.0)
        policy.evaluate(status(connected=False), 10.0)
        fired = policy.evaluate(status(connected=True), 20.0)
        self.assertEqual(fired, ())


class CollectionNotificationTests(unittest.TestCase):
    def test_refresh_failures_and_recovery_remain_silent(self) -> None:
        policy = NotificationPolicy()
        for now, failures in ((0, 0), (60, 3), (600, 10), (1000, 0)):
            self.assertEqual(
                policy.evaluate(status(connected=True, failures=failures), now), ()
            )


class EmitTests(unittest.TestCase):
    def test_emit_returns_the_copy_deck_wording(self) -> None:
        policy = NotificationPolicy()
        fired = policy.emit(NotificationKind.PAIRING_REQUIRED, 0.0)
        self.assertEqual(len(fired), 1)
        self.assertEqual(fired[0].title, tr("pairing_required"))
        self.assertEqual(fired[0].body, tr("pairing_required_body"))

    def test_emit_is_rate_limited_per_kind(self) -> None:
        policy = NotificationPolicy()
        policy.emit(NotificationKind.STARTUP_FAILED, 0.0)
        self.assertEqual(
            policy.emit(NotificationKind.STARTUP_FAILED, MIN_INTERVAL_PER_KIND_S - 1),
            (),
        )
        self.assertEqual(
            len(policy.emit(NotificationKind.STARTUP_FAILED, MIN_INTERVAL_PER_KIND_S)),
            1,
        )

    def test_device_name_is_substituted(self) -> None:
        policy = NotificationPolicy()
        fired = policy.emit(
            NotificationKind.DEVICE_LOST,
            0.0,
            device="M5StickS3",
        )
        self.assertEqual(fired[0].body, tr("panel_disconnected_body", device='M5StickS3'))


if __name__ == "__main__":
    unittest.main()
