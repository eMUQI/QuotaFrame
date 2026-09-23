from __future__ import annotations

import asyncio
import unittest
from dataclasses import fields

from quotaframe_bridge.i18n import tr
from quotaframe_bridge.service.update import BridgeUpdateMonitor, UpdateState
from quotaframe_bridge.sources.product_release import (
    ProductRelease,
    ProductReleaseError,
)
from quotaframe_bridge.versioning import SemVer


PAGE_URL = "https://github.com/eMUQI/QuotaFrame/releases/tag/v0.2.0"


def product_release(version: str = "0.2.0") -> ProductRelease:
    tag = f"v{version}"
    return ProductRelease(
        version=SemVer.parse(version),
        tag_name=tag,
        page_url=f"https://github.com/eMUQI/QuotaFrame/releases/tag/{tag}",
        manifest_url=(
            "https://github.com/eMUQI/QuotaFrame/releases/download/"
            f"{tag}/manifest.json"
        ),
    )


class FakeSource:
    def __init__(self, outcomes: list[ProductRelease | None | Exception]) -> None:
        self.outcomes = outcomes
        self.currents: list[SemVer] = []

    async def latest(self, current: SemVer) -> ProductRelease | None:
        self.currents.append(current)
        outcome = self.outcomes.pop(0)
        if isinstance(outcome, Exception):
            raise outcome
        return outcome


class FakePresenter:
    def __init__(self) -> None:
        self.states: list[UpdateState] = []
        self.notifications: list[tuple[str, str]] = []

    def set_update_state(self, state: UpdateState) -> None:
        self.states.append(state)

    def notify(self, title: str, body: str) -> None:
        self.notifications.append((title, body))


class BridgeUpdateMonitorTests(unittest.IsolatedAsyncioTestCase):
    def monitor(
        self,
        outcomes: list[ProductRelease | None | Exception],
        *,
        sleep=asyncio.sleep,
    ) -> tuple[BridgeUpdateMonitor, FakeSource, FakePresenter]:
        source = FakeSource(outcomes)
        presenter = FakePresenter()
        monitor = BridgeUpdateMonitor(
            source,  # type: ignore[arg-type]
            SemVer.parse("0.1.0"),
            presenter,
            sleep=sleep,
        )
        return monitor, source, presenter

    async def test_download_matches_running_copy_and_falls_back_to_page(self):
        from dataclasses import replace
        from itertools import product
        from unittest.mock import patch

        installer = "https://github.com/eMUQI/QuotaFrame/releases/download/v0.2.0/quotaframe-bridge-windows-v0.2.0-setup.exe"
        portable = installer.replace("-setup.exe", ".exe")
        for platform, installed, has_installer, has_portable in product(
            ("win32", "darwin"), (True, False, None), (True, False), (True, False),
        ):
            with self.subTest(platform=platform, installed=installed,
                              installer=has_installer, portable=has_portable), patch("webbrowser.open") as browser:
                release = replace(product_release(),
                                  windows_installer_url=installer if has_installer else None,
                                  windows_portable_url=portable if has_portable else None)
                presenter = FakePresenter()
                monitor = BridgeUpdateMonitor(FakeSource([release]), SemVer.parse("0.1.0"),
                                              presenter, platform=platform, windows_installed=installed)
                await monitor.check()
                expected = release.page_url
                label = "download_version"
                if platform == "win32" and installed is True and has_installer:
                    expected, label = installer, "download_installer_version"
                elif platform == "win32" and installed is False and has_portable:
                    expected, label = portable, "download_portable_version"
                self.assertEqual(presenter.states[-1].page_url, expected)
                self.assertEqual(presenter.states[-1].detail, tr(label, version="v0.2.0"))
                browser.assert_not_called()

    def test_update_state_names_menu_clickability_explicitly(self) -> None:
        self.assertEqual(
            [field.name for field in fields(UpdateState)],
            ["enabled", "detail", "page_url"],
        )

    async def test_automatic_update_publishes_state_and_notifies_once(self) -> None:
        release = product_release()
        monitor, _source, presenter = self.monitor([release, release])

        self.assertEqual(await monitor.check(), release)
        self.assertEqual(await monitor.check(), release)

        state = UpdateState(
            enabled=True,
            detail=tr("download_version", version='v0.2.0'),
            page_url=PAGE_URL,
        )
        self.assertEqual(presenter.states, [state, state])
        self.assertEqual(len(presenter.notifications), 1)
        self.assertIn("v0.2.0", " ".join(presenter.notifications[0]))

    async def test_automatic_failure_only_writes_a_sanitized_log(self) -> None:
        monitor, _source, presenter = self.monitor(
            [ProductReleaseError("secret response body")]
        )

        with self.assertLogs(
            "quotaframe_bridge.service.update", level="WARNING"
        ) as captured:
            self.assertIsNone(await monitor.check())

        self.assertEqual(presenter.states, [])
        self.assertEqual(presenter.notifications, [])
        self.assertNotIn("secret response body", "\n".join(captured.output))

    async def test_automatic_no_update_leaves_manual_check_enabled(self) -> None:
        monitor, _source, presenter = self.monitor([None])

        self.assertIsNone(await monitor.check())

        self.assertEqual(
            presenter.states,
            [UpdateState(True, tr("check_bridge_update"), None)],
        )

    async def test_manual_no_update_restores_action_and_notifies(self) -> None:
        monitor, _source, presenter = self.monitor([None])

        self.assertIsNone(await monitor.check(manual=True))

        self.assertEqual(
            presenter.states,
            [
                UpdateState(False, tr("checking_ellipsis"), None),
                UpdateState(True, tr("check_bridge_update"), None),
            ],
        )
        self.assertEqual(presenter.notifications[0][0], tr("bridge_current"))

    async def test_current_release_is_not_presented_as_bridge_update(self) -> None:
        monitor, _source, presenter = self.monitor([product_release("0.1.0")])

        self.assertIsNone(await monitor.check(manual=True))

        self.assertEqual(
            presenter.states,
            [
                UpdateState(False, tr("checking_ellipsis"), None),
                UpdateState(True, tr("check_bridge_update"), None),
            ],
        )
        self.assertEqual(presenter.notifications[0][0], tr("bridge_current"))

    async def test_manual_update_checks_before_presenting_download(self) -> None:
        release = product_release()
        monitor, _source, presenter = self.monitor([release])

        self.assertEqual(await monitor.check(manual=True), release)

        self.assertEqual(
            presenter.states,
            [
                UpdateState(False, tr("checking_ellipsis"), None),
                UpdateState(True, tr("download_version", version='v0.2.0'), PAGE_URL),
            ],
        )

    async def test_manual_check_repeats_available_result_after_automatic_notice(self) -> None:
        release = product_release()
        monitor, _source, presenter = self.monitor([release] * 4)

        await monitor.check()
        await monitor.check(manual=True)
        await monitor.check(manual=True)
        await monitor.check()

        self.assertEqual(len(presenter.notifications), 3)
        self.assertTrue(all("v0.2.0" in body for _, body in presenter.notifications))

    async def test_manual_failure_notifies_without_exception_text(self) -> None:
        monitor, _source, presenter = self.monitor(
            [ProductReleaseError("private API detail")]
        )

        with self.assertLogs(
            "quotaframe_bridge.service.update", level="WARNING"
        ):
            self.assertIsNone(await monitor.check(manual=True))

        rendered = " ".join(sum(presenter.notifications, ()))
        self.assertIn(tr("update_check_failed"), rendered)
        self.assertNotIn("private API detail", rendered)
        self.assertEqual(
            presenter.states,
            [
                UpdateState(False, tr("checking_ellipsis"), None),
                UpdateState(True, tr("check_bridge_update"), None),
            ],
        )

    async def test_run_checks_immediately_then_every_86400_seconds(self) -> None:
        sleeps: list[float] = []

        async def sleep(delay: float) -> None:
            sleeps.append(delay)
            if len(sleeps) == 2:
                raise asyncio.CancelledError

        monitor, source, presenter = self.monitor([None, None], sleep=sleep)

        with self.assertRaises(asyncio.CancelledError):
            await monitor.run()

        self.assertEqual(sleeps, [86400, 86400])
        self.assertEqual(len(source.currents), 2)
        self.assertEqual(
            presenter.states,
            [
                UpdateState(True, tr("check_bridge_update"), None),
                UpdateState(True, tr("check_bridge_update"), None),
            ],
        )

    async def test_check_cancellation_propagates_without_notification(self) -> None:
        class CancellingSource:
            async def latest(self, _current: SemVer) -> ProductRelease | None:
                raise asyncio.CancelledError

        presenter = FakePresenter()
        monitor = BridgeUpdateMonitor(
            CancellingSource(),  # type: ignore[arg-type]
            SemVer.parse("0.1.0"),
            presenter,
        )

        with self.assertRaises(asyncio.CancelledError):
            await monitor.check(manual=True)
        self.assertEqual(presenter.notifications, [])
        self.assertEqual(
            presenter.states,
            [UpdateState(False, tr("checking_ellipsis"), None)],
        )


if __name__ == "__main__":
    unittest.main()
