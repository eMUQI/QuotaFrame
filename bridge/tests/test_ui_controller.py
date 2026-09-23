from __future__ import annotations

import unittest

from quotaframe_bridge.i18n import tr
from quotaframe_bridge.domain.models import Provider, ProviderUsage, SourceState
from quotaframe_bridge.ui.controller import TrayController
from quotaframe_bridge.ui.notifications import (
    DEVICE_LOST_AFTER_S,
    NotificationKind,
    NotificationPolicy,
)
from quotaframe_bridge.ui.status import DeviceStatus, PanelStatus, TrayState
from ui_fakes import FakeTrayShell


def unavailable(provider: Provider) -> ProviderUsage:
    return ProviderUsage(
        provider=provider,
        state=SourceState.UNAVAILABLE,
        sampled_at=1,
    )


def status(*, connected: bool) -> PanelStatus:
    return PanelStatus(
        devices=(
            DeviceStatus("M5", connected=connected, ever_connected=True),
        ),
        providers={
            Provider.CODEX: unavailable(Provider.CODEX),
            Provider.CLAUDE: unavailable(Provider.CLAUDE),
        },
        consecutive_failures=0,
        resolution_error=None,
    )


class TrayControllerTests(unittest.TestCase):
    def setUp(self) -> None:
        self.shell = FakeTrayShell()
        self.controller = TrayController(self.shell, NotificationPolicy())

    def test_apply_pushes_state_tooltip_and_info_lines(self) -> None:
        self.controller.apply(status(connected=True), 0.0)
        self.assertEqual(self.shell.states, [TrayState.OK])
        self.assertEqual(len(self.shell.tooltips), 1)
        self.assertEqual(len(self.shell.info_lines), 1)
        self.assertEqual(self.shell.notifications, [])

    def test_apply_forwards_earned_notifications(self) -> None:
        self.controller.apply(status(connected=True), 0.0)
        self.controller.apply(status(connected=False), 10.0)
        self.controller.apply(status(connected=False), 10.0 + DEVICE_LOST_AFTER_S)
        self.assertEqual(
            self.shell.notifications,
            [(tr("panel_disconnected"), tr("panel_disconnected_body", device='M5'))],
        )

    def test_one_failing_stage_does_not_suppress_the_others(self) -> None:
        """A single render failure must not freeze the tray on its placeholder."""

        def explode(_state: TrayState) -> None:
            raise KeyError(Provider.CODEX)

        self.shell.set_state = explode  # type: ignore[method-assign]

        with self.assertLogs("quotaframe_bridge.ui.controller", level="ERROR"):
            self.controller.apply(status(connected=True), 0.0)

        self.assertEqual(len(self.shell.info_lines), 1)
        self.assertEqual(len(self.shell.tooltips), 1)

    def test_announce_forwards_a_direct_notification(self) -> None:
        self.controller.announce(NotificationKind.PAIRING_REQUIRED, 0.0)
        self.assertEqual(
            self.shell.notifications,
            [(tr("pairing_required"), tr("pairing_required_body"))],
        )

    def test_bridge_update_action_is_forwarded_to_the_shell(self) -> None:
        self.controller.set_bridge_update_action(False, tr("checking"))
        self.controller.set_bridge_update_action(True, tr("download_version", version='v0.2.0'))

        self.assertEqual(
            self.shell.bridge_update_actions,
            [(False, tr("checking")), (True, tr("download_version", version='v0.2.0'))],
        )

    def test_repeated_identical_status_does_not_repeat_notifications(self) -> None:
        self.controller.apply(status(connected=True), 0.0)
        self.controller.apply(status(connected=False), 10.0)
        for tick in range(5):
            self.controller.apply(
                status(connected=False),
                10.0 + DEVICE_LOST_AFTER_S + tick,
            )
        self.assertEqual(len(self.shell.notifications), 1)


if __name__ == "__main__":
    unittest.main()
