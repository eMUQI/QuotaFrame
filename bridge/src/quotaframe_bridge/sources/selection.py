"""Pick the usage source that matches the host platform."""

from __future__ import annotations

import sys
from collections.abc import Callable
from pathlib import Path

from quotaframe_bridge.sources.base import UsageSource

SUPPORTED_PLATFORMS = ("win32", "darwin")


class UnsupportedPlatformError(RuntimeError):
    """No CodexBar data source exists for this operating system."""

    def __init__(self, platform: str) -> None:
        self.platform = platform
        super().__init__(
            f"no usage source for platform {platform!r}; "
            "the Bridge supports Windows and macOS"
        )


def create_usage_source(
    executable: Path | None = None,
    *,
    timeout: float | None = None,
    platform: str | None = None,
    dependency_status: Callable[[str], None] | None = None,
) -> UsageSource:
    """Return the Win-CodexBar or upstream CodexBar adapter for this host.

    `timeout` of None means "whatever this platform's CLI needs"; the two
    differ by an order of magnitude, see `CliDialect.default_timeout`.
    """

    platform = sys.platform if platform is None else platform
    if platform == "win32":
        from quotaframe_bridge.sources.codexbar import WinCodexBarSource

        return WinCodexBarSource(executable, timeout=timeout, dependency_status=dependency_status)
    if platform == "darwin":
        from quotaframe_bridge.sources.codexbar_mac import MacCodexBarSource

        return MacCodexBarSource(executable, timeout=timeout)
    raise UnsupportedPlatformError(platform)
