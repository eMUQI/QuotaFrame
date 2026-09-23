from __future__ import annotations

from quotaframe_bridge.i18n import language, tr

import unittest
from unittest.mock import patch

from quotaframe_bridge.domain.models import (
    Provider,
    ProviderUsage,
    SourceState,
    UsageWindow,
)
from quotaframe_bridge.ui.status import (
    DEVICE_SUMMARY_WIDTH,
    NO_DEVICES_SCANNING_TEXT,
    NO_DEVICES_TEXT,
    DeviceStatus,
    PanelStatus,
    TrayState,
    display_width,
    format_device_summary,
    format_info_lines,
    format_tooltip,
)


def usage(provider: Provider, percent: int | None) -> ProviderUsage:
    if percent is None:
        return ProviderUsage(
            provider=provider,
            state=SourceState.UNAVAILABLE,
            sampled_at=1,
        )
    return ProviderUsage(
        provider=provider,
        state=SourceState.OK,
        sampled_at=1,
        short=UsageWindow(used_percent=percent),
        week=UsageWindow(used_percent=percent),
    )


def status(
    devices: tuple[DeviceStatus, ...],
    *,
    failures: int = 0,
    resolution_error: str | None = None,
    codex: int | None = 42,
    claude: int | None = 17,
) -> PanelStatus:
    return PanelStatus(
        devices=devices,
        providers={
            Provider.CODEX: usage(Provider.CODEX, codex),
            Provider.CLAUDE: usage(Provider.CLAUDE, claude),
        },
        consecutive_failures=failures,
        resolution_error=resolution_error,
    )


class TrayStateTests(unittest.TestCase):
    def test_incompatible_device_guidance_survives_multi_device_summary(self) -> None:
        devices = (
            DeviceStatus("Waveshare" * 10, False, False, incompatible=True),
            DeviceStatus("M5", False, True),
            DeviceStatus("Other", True, True),
        )
        text = format_info_lines(status(devices))[0]
        self.assertEqual(text, "1 台需更新 Bridge/固件")
        self.assertLessEqual(display_width(text), DEVICE_SUMMARY_WIDTH)

    def test_all_connected_and_collecting_is_ok(self) -> None:
        snapshot = status(
            (
                DeviceStatus("M5", connected=True, ever_connected=True),
                DeviceStatus("WS", connected=True, ever_connected=True),
            )
        )
        self.assertIs(snapshot.state, TrayState.OK)

    def test_never_connected_second_board_does_not_warn(self) -> None:
        """A user who owns one board must not sit at WARN forever."""

        snapshot = status(
            (
                DeviceStatus("M5", connected=True, ever_connected=True),
                DeviceStatus("WS", connected=False, ever_connected=False),
            )
        )
        self.assertIs(snapshot.state, TrayState.OK)

    def test_previously_connected_board_that_dropped_warns(self) -> None:
        snapshot = status(
            (
                DeviceStatus("M5", connected=False, ever_connected=True),
                DeviceStatus("WS", connected=False, ever_connected=False),
            )
        )
        self.assertIs(snapshot.state, TrayState.WARN)

    def test_no_board_ever_connected_is_idle(self) -> None:
        snapshot = status(
            (
                DeviceStatus("M5", connected=False, ever_connected=False),
                DeviceStatus("WS", connected=False, ever_connected=False),
            )
        )
        self.assertIs(snapshot.state, TrayState.IDLE)

    def test_resolution_error_is_idle_even_when_connected(self) -> None:
        snapshot = status(
            (DeviceStatus("M5", connected=True, ever_connected=True),),
            resolution_error="not_found",
        )
        self.assertIs(snapshot.state, TrayState.IDLE)

    def test_repeated_collection_failures_do_not_warn(self) -> None:
        snapshot = status(
            (DeviceStatus("M5", connected=True, ever_connected=True),),
            failures=3,
        )
        self.assertIs(snapshot.state, TrayState.OK)

    def test_one_collection_failure_does_not_warn(self) -> None:
        snapshot = status(
            (DeviceStatus("M5", connected=True, ever_connected=True),),
            failures=1,
        )
        self.assertIs(snapshot.state, TrayState.OK)


class TrayTextTests(unittest.TestCase):
    def test_week_only_usage_is_labelled_in_both_languages(self) -> None:
        snapshot = PanelStatus(
            devices=(),
            providers={Provider.CODEX: ProviderUsage(
                provider=Provider.CODEX, state=SourceState.PARTIAL,
                sampled_at=1, week=UsageWindow(used_percent=73),
            )},
            consecutive_failures=0, resolution_error=None,
        )
        for lang, expected in (("zh", "Codex 周 73% 已用"),
                               ("en", "Codex week 73% used")):
            with patch("quotaframe_bridge.i18n.language", return_value=lang):
                self.assertIn(expected, format_info_lines(snapshot)[1])

    def test_tooltip_uses_the_bounded_device_summary(self) -> None:
        snapshot = status(
            (
                DeviceStatus("M5", connected=True, ever_connected=True),
                DeviceStatus("WS", connected=False, ever_connected=False),
            )
        )
        self.assertEqual(
            format_tooltip(snapshot),
            "QuotaFrame · " + tr("named_exception", device="WS " + tr("not_found"), separator=" · ", connected=1) + f" · Codex {tr('usage_short')} 42% {tr('usage_used')} · Claude {tr('usage_short')} 17% {tr('usage_used')}",
        )

    def test_tooltip_distinguishes_dropped_from_never_seen(self) -> None:
        snapshot = status(
            (DeviceStatus("M5", connected=False, ever_connected=True),)
        )
        self.assertIn("M5 " + tr("disconnected"), format_tooltip(snapshot))

    def test_tooltip_never_exceeds_the_windows_limit(self) -> None:
        snapshot = status(
            tuple(
                DeviceStatus(f"DEVICE-{index:02d}", connected=True, ever_connected=True)
                for index in range(20)
            )
        )
        self.assertLessEqual(len(format_tooltip(snapshot)), 127)

    def test_missing_usage_renders_as_dashes(self) -> None:
        snapshot = status(
            (DeviceStatus("M5", connected=True, ever_connected=True),),
            codex=None,
        )
        self.assertIn("Codex --", format_tooltip(snapshot))

    def test_empty_startup_usage_renders_as_dashes(self) -> None:
        """The first tray tick arrives before the initial collection finishes."""

        snapshot = PanelStatus(
            devices=(DeviceStatus("M5", connected=False, ever_connected=False),),
            providers={},
            consecutive_failures=0,
            resolution_error=None,
        )

        self.assertEqual(
            format_info_lines(snapshot),
            ("M5 " + tr("not_found"), "Codex -- · Claude --"),
        )

    def test_info_lines_split_devices_from_providers(self) -> None:
        snapshot = status(
            (
                DeviceStatus("M5", connected=True, ever_connected=True),
                DeviceStatus("WS", connected=False, ever_connected=False),
            )
        )
        self.assertEqual(
            format_info_lines(snapshot),
            (tr("named_exception", device="WS " + tr("not_found"), separator=' · ', connected='1'), f"Codex {tr('usage_short')} 42% {tr('usage_used')} · Claude {tr('usage_short')} 17% {tr('usage_used')}"),
        )


class DeviceSummaryTests(unittest.TestCase):
    def test_zero_devices_uses_the_adoption_empty_state(self) -> None:
        self.assertEqual(format_device_summary(()), NO_DEVICES_TEXT)

    def test_one_short_device_keeps_its_name_and_precise_state(self) -> None:
        devices = (DeviceStatus("M5StickS3", True, True),)

        self.assertEqual(format_device_summary(devices), "M5StickS3 " + tr("connected"))

    def test_one_long_device_keeps_state_inside_the_width_budget(self) -> None:
        devices = (DeviceStatus("W" * 80, False, True),)

        summary = format_device_summary(devices)

        self.assertEqual(summary, tr("single_device", separator=' · ', state=tr("disconnected")))
        self.assertLessEqual(display_width(summary), DEVICE_SUMMARY_WIDTH)

    def test_two_connected_devices_use_a_fixed_dimension_summary(self) -> None:
        devices = (
            DeviceStatus("M5", True, True),
            DeviceStatus("WS", True, True),
        )

        self.assertEqual(format_device_summary(devices), tr("all_connected", count='2', separator=' · '))

    def test_unique_exception_is_named_when_it_fits(self) -> None:
        devices = (
            DeviceStatus("M5", True, True),
            DeviceStatus("Waveshare", False, True),
            DeviceStatus("Desk", True, True),
        )

        self.assertEqual(
            format_device_summary(devices),
            tr("named_exception", device="Waveshare " + tr("disconnected"), separator=' · ', connected='2'),
        )

    def test_disambiguated_exception_respects_localized_width(self) -> None:
        devices = (
            DeviceStatus("Waveshare ·66A1", False, True),
            DeviceStatus("Waveshare ·66B2", True, True),
        )

        summary = format_device_summary(devices)

        expected = (
            tr(
                "named_exception", device="Waveshare ·66A1 " + tr("disconnected"),
                separator=" · ", connected=1,
            )
            if language() == "zh"
            else tr("device_counts", count=2, separator=" · ", connected=1, disconnected=1)
        )
        self.assertEqual(summary, expected)
        self.assertLessEqual(display_width(summary), DEVICE_SUMMARY_WIDTH)

    def test_long_unique_exception_falls_back_to_counts(self) -> None:
        devices = (
            DeviceStatus("M5", True, True),
            DeviceStatus("W" * 80, False, False),
            DeviceStatus("Desk", True, True),
        )

        summary = format_device_summary(devices)

        self.assertEqual(summary, tr("device_counts", count='3', separator=' · ', connected='2', disconnected='1'))
        self.assertLessEqual(display_width(summary), DEVICE_SUMMARY_WIDTH)

    def test_multiple_exceptions_use_connected_and_unconnected_counts(self) -> None:
        devices = (
            DeviceStatus("A", True, True),
            DeviceStatus("B", False, True),
            DeviceStatus("C", True, True),
            DeviceStatus("D", False, False),
            DeviceStatus("E", True, True),
        )

        self.assertEqual(
            format_device_summary(devices),
            tr("device_counts", count='5', separator=' · ', connected='3', disconnected='2'),
        )

    def test_twenty_devices_do_not_grow_the_summary(self) -> None:
        devices = tuple(
            DeviceStatus(f"DEVICE-{index:02d}", index < 18, True)
            for index in range(20)
        )

        summary = format_device_summary(devices)

        self.assertEqual(summary, tr("device_counts", count='20', separator=' · ', connected='18', disconnected='2'))
        self.assertLessEqual(display_width(summary), DEVICE_SUMMARY_WIDTH)


class EmptyOwnershipTests(unittest.TestCase):
    """A user who has adopted nothing must not be told a board is missing."""

    def test_device_row_invites_adoption_instead_of_listing_absences(self) -> None:
        status = PanelStatus(
            devices=(),
            providers={},
            consecutive_failures=0,
            resolution_error=None,
        )

        devices, _providers = format_info_lines(status)

        self.assertEqual(devices, NO_DEVICES_TEXT)
        self.assertNotIn("正在查找", devices)
        self.assertNotIn(tr("not_found"), devices)

    def test_device_row_reports_an_active_adoption_scan(self) -> None:
        status = PanelStatus(
            devices=(),
            providers={},
            consecutive_failures=0,
            resolution_error=None,
            adoption_running=True,
        )

        devices, _providers = format_info_lines(status)

        self.assertEqual(devices, NO_DEVICES_SCANNING_TEXT)

    def test_owning_nothing_is_idle_rather_than_a_warning(self) -> None:
        status = PanelStatus(
            devices=(),
            providers={},
            consecutive_failures=0,
            resolution_error=None,
        )

        self.assertIs(status.state, TrayState.IDLE)


if __name__ == "__main__":
    unittest.main()
