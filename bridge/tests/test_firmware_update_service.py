from __future__ import annotations

import asyncio
import unittest

from quotaframe_bridge.i18n import tr
from quotaframe_bridge.service.firmware_update import (
    FirmwareUpdateMonitor,
    FirmwareUpdatePresentation,
)
from quotaframe_bridge.sources.firmware_release import (
    FirmwareImage,
    FirmwareManifest,
    FirmwareReleaseError,
)


MANIFEST_URL = (
    "https://github.com/eMUQI/QuotaFrame/releases/download/v0.3.0/manifest.json"
)


def firmware_image(
    target: str = "waveshare_amoled_216",
    version: str = "0.3.0",
) -> FirmwareImage:
    return FirmwareImage(
        target=target,
        version=version,
        size=4,
        sha256="00" * 32,
        url=(
            "https://github.com/eMUQI/QuotaFrame/releases/download/"
            f"v{version}/{target}.bin"
        ),
    )


class FakeSource:
    def __init__(self, outcomes: list[FirmwareManifest | Exception]) -> None:
        self.outcomes = outcomes
        self.manifest_urls: list[str] = []
        self.downloads: list[FirmwareImage] = []

    async def fetch_manifest(self, url: str) -> FirmwareManifest:
        self.manifest_urls.append(url)
        outcome = self.outcomes.pop(0)
        if isinstance(outcome, Exception):
            raise outcome
        return outcome

    async def download_image(self, image: FirmwareImage) -> bytes:
        self.downloads.append(image)
        raise AssertionError("automatic checks must not download firmware images")


class FirmwareUpdateMonitorTests(unittest.IsolatedAsyncioTestCase):
    def monitor(
        self,
        outcomes: list[FirmwareManifest | Exception],
        *,
        url=lambda: MANIFEST_URL,
        sleep=asyncio.sleep,
    ) -> tuple[FirmwareUpdateMonitor, FakeSource]:
        source = FakeSource(outcomes)
        monitor = FirmwareUpdateMonitor(
            source,  # type: ignore[arg-type]
            url,
            sleep=sleep,
        )
        return monitor, source

    async def test_check_fetches_only_the_manifest(self) -> None:
        manifest = FirmwareManifest((firmware_image(),))
        monitor, source = self.monitor([manifest])

        self.assertEqual(await monitor.check(), manifest)

        self.assertEqual(source.manifest_urls, [MANIFEST_URL])
        self.assertEqual(source.downloads, [])

    async def test_newer_release_is_clickable_and_notifies_once_per_target_version(self) -> None:
        manifest = FirmwareManifest(
            (
                firmware_image(),
                firmware_image("m5sticks3"),
            )
        )
        monitor, _source = self.monitor([manifest])
        await monitor.check()

        first = monitor.presentation("WS", "waveshare_amoled_216", "0.2.0")
        repeated = monitor.presentation("WS", "waveshare_amoled_216", "0.2.0")
        same_target = monitor.presentation(
            "WS-备用", "waveshare_amoled_216", "0.2.0"
        )
        other_target = monitor.presentation("M5", "m5sticks3", "0.2.0")

        self.assertEqual(
            first,
            FirmwareUpdatePresentation(
                enabled=True,
                detail=tr("firmware_version_available", version='0.3.0'),
                notification=(
                    tr("firmware_available", label='WS'),
                    tr("firmware_available_body", current_version='0.2.0', version='0.3.0'),
                ),
            ),
        )
        self.assertIsNone(repeated.notification)
        self.assertIsNone(same_target.notification)
        self.assertEqual(other_target.notification[0], tr("firmware_available", label='M5'))

    async def test_git_describe_device_is_offered_a_newer_release(self) -> None:
        manifest = FirmwareManifest((firmware_image(),))
        monitor, _source = self.monitor([manifest])
        await monitor.check()

        presentation = monitor.presentation(
            "WS", "waveshare_amoled_216", "v0.2.0-26-g921badd"
        )

        self.assertTrue(presentation.enabled)
        self.assertEqual(presentation.detail, tr("firmware_version_available", version='0.3.0'))

    async def test_same_git_describe_release_is_already_current(self) -> None:
        manifest = FirmwareManifest((firmware_image(),))
        monitor, _source = self.monitor([manifest])
        await monitor.check()

        self.assertEqual(
            monitor.presentation(
                "WS", "waveshare_amoled_216", "v0.3.0-26-g921badd"
            ),
            FirmwareUpdatePresentation(False, tr("up_to_date")),
        )

    async def test_equal_or_newer_device_version_is_already_current(self) -> None:
        manifest = FirmwareManifest((firmware_image(),))
        monitor, _source = self.monitor([manifest])
        await monitor.check()

        for current in ("0.3.0", "0.4.0"):
            with self.subTest(current=current):
                self.assertEqual(
                    monitor.presentation("WS", "waveshare_amoled_216", current),
                    FirmwareUpdatePresentation(False, tr("up_to_date")),
                )

    async def test_semver_prerelease_precedence_is_used(self) -> None:
        manifest = FirmwareManifest((firmware_image(version="0.3.0-rc.2"),))
        monitor, _source = self.monitor([manifest])
        await monitor.check()

        presentation = monitor.presentation(
            "WS", "waveshare_amoled_216", "0.3.0-rc.1"
        )

        self.assertTrue(presentation.enabled)
        self.assertEqual(presentation.detail, tr("firmware_version_available", version='0.3.0-rc.2'))

    async def test_invalid_device_version_keeps_manual_retry_without_leaking_value(self) -> None:
        manifest = FirmwareManifest((firmware_image(),))
        monitor, _source = self.monitor([manifest])
        await monitor.check()

        with self.assertLogs(
            "quotaframe_bridge.service.firmware_update", level="WARNING"
        ) as captured:
            first = monitor.presentation(
                "WS", "waveshare_amoled_216", "private invalid version"
            )
            second = monitor.presentation(
                "WS", "waveshare_amoled_216", "private invalid version"
            )

        self.assertEqual(first, FirmwareUpdatePresentation(False, tr("unavailable")))
        self.assertEqual(second, first)
        self.assertEqual(len(captured.output), 1)
        self.assertNotIn("private invalid version", captured.output[0])

    async def test_failed_check_is_silent_and_keeps_manual_retry(self) -> None:
        monitor, _source = self.monitor(
            [FirmwareReleaseError("private response body")]
        )

        with self.assertLogs(
            "quotaframe_bridge.service.firmware_update", level="WARNING"
        ) as captured:
            self.assertIsNone(await monitor.check())

        self.assertEqual(
            monitor.presentation("WS", "waveshare_amoled_216", "0.2.0"),
            FirmwareUpdatePresentation(True, tr("check_and_update")),
        )
        self.assertNotIn("private response body", "\n".join(captured.output))

    async def test_waiting_and_inflight_checks_are_not_clickable(self) -> None:
        entered = asyncio.Event()
        release = asyncio.Event()

        class BlockingSource:
            async def fetch_manifest(self, _url: str) -> FirmwareManifest:
                entered.set()
                await release.wait()
                return FirmwareManifest((firmware_image(),))

        monitor = FirmwareUpdateMonitor(BlockingSource(), lambda: MANIFEST_URL)  # type: ignore[arg-type]
        self.assertEqual(
            monitor.presentation("WS", "waveshare_amoled_216", "0.2.0"),
            FirmwareUpdatePresentation(False, tr("checking")),
        )

        task = asyncio.create_task(monitor.check())
        await entered.wait()
        self.assertEqual(
            monitor.presentation("WS", "waveshare_amoled_216", "0.2.0"),
            FirmwareUpdatePresentation(False, tr("checking")),
        )
        release.set()
        await task

    async def test_run_checks_immediately_then_every_86400_seconds(self) -> None:
        manifest = FirmwareManifest((firmware_image(),))
        sleeps: list[float] = []

        async def sleep(delay: float) -> None:
            sleeps.append(delay)
            if len(sleeps) == 2:
                raise asyncio.CancelledError

        monitor, source = self.monitor([manifest, manifest], sleep=sleep)

        with self.assertRaises(asyncio.CancelledError):
            await monitor.run()

        self.assertEqual(sleeps, [86400, 86400])
        self.assertEqual(source.manifest_urls, [MANIFEST_URL, MANIFEST_URL])

    async def test_run_waits_five_seconds_for_the_first_manifest_url(self) -> None:
        manifest = FirmwareManifest((firmware_image(),))
        urls: list[str | None] = [None, MANIFEST_URL]
        sleeps: list[float] = []

        def url() -> str | None:
            return urls.pop(0) if urls else MANIFEST_URL

        async def sleep(delay: float) -> None:
            sleeps.append(delay)
            if len(sleeps) == 2:
                raise asyncio.CancelledError

        monitor, source = self.monitor([manifest], url=url, sleep=sleep)

        with self.assertRaises(asyncio.CancelledError):
            await monitor.run()

        self.assertEqual(sleeps, [5, 86400])
        self.assertEqual(source.manifest_urls, [MANIFEST_URL])

    async def test_check_cancellation_propagates(self) -> None:
        class CancellingSource:
            async def fetch_manifest(self, _url: str) -> FirmwareManifest:
                raise asyncio.CancelledError

        monitor = FirmwareUpdateMonitor(CancellingSource(), lambda: MANIFEST_URL)  # type: ignore[arg-type]

        with self.assertRaises(asyncio.CancelledError):
            await monitor.check()


if __name__ == "__main__":
    unittest.main()
