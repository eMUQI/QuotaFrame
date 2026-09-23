"""Toolkit-neutral device firmware availability checks."""

from __future__ import annotations

import asyncio
import logging
from collections.abc import Awaitable, Callable
from dataclasses import dataclass

from quotaframe_bridge.i18n import tr
from quotaframe_bridge.sources.firmware_release import (
    FirmwareManifest,
    FirmwareReleaseError,
    FirmwareReleaseSource,
)
from quotaframe_bridge.versioning import SemVer, VersionError


logger = logging.getLogger(__name__)
Sleep = Callable[[float], Awaitable[None]]
ManifestUrl = Callable[[], str | None]


@dataclass(frozen=True, slots=True)
class FirmwareUpdatePresentation:
    enabled: bool
    detail: str
    notification: tuple[str, str] | None = None


class FirmwareUpdateMonitor:
    """Refresh one release manifest and project it onto connected devices."""

    def __init__(
        self,
        source: FirmwareReleaseSource,
        manifest_url: ManifestUrl,
        *,
        interval: float = 86400,
        wait_interval: float = 5,
        sleep: Sleep = asyncio.sleep,
    ) -> None:
        self._source = source
        self._manifest_url = manifest_url
        self._interval = interval
        self._wait_interval = wait_interval
        self._sleep = sleep
        self._state = "waiting"
        self._configured = False
        self._manifest: FirmwareManifest | None = None
        self._notified: set[tuple[object, str, str, str]] = set()
        self._invalid: set[tuple[str, str, str]] = set()

    async def check(self) -> FirmwareManifest | None:
        url = self._manifest_url()
        self._configured = bool(url)
        if not url:
            self._state = "waiting"
            self._manifest = None
            return None

        self._state = "checking"
        try:
            manifest = await self._source.fetch_manifest(url)
        except FirmwareReleaseError as exc:
            logger.warning(
                "firmware update check failed: %s", type(exc).__name__
            )
            self._state = "failed"
            self._manifest = None
            return None

        self._manifest = manifest
        self._state = "ready"
        return manifest

    async def run(self) -> None:
        while True:
            await self.check()
            delay = self._interval if self._configured else self._wait_interval
            await self._sleep(delay)

    def presentation(
        self,
        label: str,
        target: str,
        current_version: str,
        firmware_project: str = "quotaframe",
        session: object = None,
    ) -> FirmwareUpdatePresentation:
        if self._state in {"waiting", "checking"}:
            return FirmwareUpdatePresentation(False, tr("checking"))
        if self._state == "failed" or self._manifest is None:
            return FirmwareUpdatePresentation(True, tr("check_and_update"))

        try:
            image = self._manifest.for_target(target, firmware_project)
            current = SemVer.from_device(current_version)
            latest = SemVer.parse(image.version)
        except (FirmwareReleaseError, VersionError):
            invalid = (label, target, current_version)
            if invalid not in self._invalid:
                self._invalid.add(invalid)
                logger.warning(
                    "firmware version comparison failed: device=%s target=%s",
                    label,
                    target,
                )
            return FirmwareUpdatePresentation(False, tr("unavailable"))

        if latest <= current:
            return FirmwareUpdatePresentation(False, tr("up_to_date"))

        notified = (session, firmware_project, target, image.version)
        notification = None
        if notified not in self._notified:
            self._notified.add(notified)
            notification = (
                tr("firmware_available", label=label),
                tr(
                    "firmware_available_body", current_version=current_version,
                    version=image.version,
                ),
            )
        return FirmwareUpdatePresentation(
            True,
            tr("firmware_version_available", version=image.version),
            notification,
        )
