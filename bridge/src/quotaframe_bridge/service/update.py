"""Toolkit-neutral policy for Bridge product update checks."""

from __future__ import annotations

import asyncio
import logging
import sys
from collections.abc import Awaitable, Callable
from dataclasses import dataclass
from typing import Protocol

from quotaframe_bridge.i18n import tr
from quotaframe_bridge.sources.product_release import (
    ProductRelease,
    ProductReleaseError,
    ProductReleaseSource,
)
from quotaframe_bridge.versioning import SemVer


logger = logging.getLogger(__name__)
Sleep = Callable[[float], Awaitable[None]]


@dataclass(frozen=True, slots=True)
class UpdateState:
    enabled: bool
    detail: str
    page_url: str | None


IDLE_STATE = UpdateState(True, tr("check_bridge_update"), None)
CHECKING_STATE = UpdateState(False, tr("checking_ellipsis"), None)


class UpdatePresenter(Protocol):
    def set_update_state(self, state: UpdateState) -> None:
        pass

    def notify(self, title: str, body: str) -> None:
        pass


class BridgeUpdateMonitor:
    def __init__(
        self,
        source: ProductReleaseSource,
        current: SemVer,
        presenter: UpdatePresenter,
        *,
        platform: str = sys.platform,
        windows_installed: bool | None = None,
        interval: float = 86400,
        sleep: Sleep = asyncio.sleep,
    ) -> None:
        self._platform = platform
        self._windows_installed = windows_installed
        self._source = source
        self._current = current
        self._presenter = presenter
        self._interval = interval
        self._sleep = sleep
        self._notified: set[SemVer] = set()

    async def check(self, *, manual: bool = False) -> ProductRelease | None:
        return await self._check_and_present(manual=manual)

    async def run(self) -> None:
        while True:
            await self._check_and_present(manual=False)
            await self._sleep(self._interval)

    async def _check_and_present(
        self,
        *,
        manual: bool,
    ) -> ProductRelease | None:
        if manual:
            self._presenter.set_update_state(CHECKING_STATE)
        try:
            release = await self._source.latest(self._current)
        except ProductReleaseError as exc:
            logger.warning("Bridge update check failed: %s", type(exc).__name__)
            if manual:
                self._presenter.set_update_state(IDLE_STATE)
                self._presenter.notify(tr("update_check_failed"), tr("retry_later"))
            return None

        if release is None or release.version <= self._current:
            self._presenter.set_update_state(IDLE_STATE)
            if manual:
                self._presenter.notify(
                    tr("bridge_current"),
                    tr("bridge_current_body", version=self._current),
                )
            return None

        download_url = None
        detail_key = "download_version"
        body_key = "bridge_available_body"
        if self._platform == "win32":
            if self._windows_installed is True:
                download_url = release.windows_installer_url
                if download_url:
                    detail_key = "download_installer_version"
                    body_key = "bridge_installer_available_body"
            elif self._windows_installed is False:
                download_url = release.windows_portable_url
                if download_url:
                    detail_key = "download_portable_version"
                    body_key = "bridge_portable_available_body"

        self._presenter.set_update_state(
            UpdateState(
                enabled=True,
                detail=tr(detail_key, version=release.tag_name),
                page_url=download_url or release.page_url,
            )
        )
        if manual or release.version not in self._notified:
            self._notified.add(release.version)
            self._presenter.notify(
                tr("bridge_available"),
                tr(body_key, version=release.tag_name),
            )
        return release
