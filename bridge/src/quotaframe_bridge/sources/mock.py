"""Deterministic account-free usage source."""

from __future__ import annotations

import time

from quotaframe_bridge.domain.models import (
    Provider,
    ProviderUsage,
    SourceState,
    UsageWindow,
)


class MockUsageSource:
    """Emit changing but repeatable usage values and future reset times."""

    def __init__(self) -> None:
        self._cycle = 0

    async def collect(
        self, attempted_at: int | None = None
    ) -> dict[Provider, ProviderUsage]:
        now = int(time.time()) if attempted_at is None else attempted_at
        cycle = self._cycle
        self._cycle += 1
        return {
            Provider.CODEX: ProviderUsage(
                provider=Provider.CODEX,
                state=SourceState.OK,
                sampled_at=now,
                short=UsageWindow((12 + 7 * cycle) % 101, now + 2 * 60 * 60),
                week=UsageWindow((34 + 3 * cycle) % 101, now + 4 * 24 * 60 * 60),
            ),
            Provider.CLAUDE: ProviderUsage(
                provider=Provider.CLAUDE,
                state=SourceState.OK,
                sampled_at=now,
                short=UsageWindow((27 + 5 * cycle) % 101, now + 75 * 60),
                week=UsageWindow((48 + 2 * cycle) % 101, now + 3 * 24 * 60 * 60),
            ),
        }
