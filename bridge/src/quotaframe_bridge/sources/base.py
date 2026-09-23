"""Source adapter contract."""

from __future__ import annotations

from typing import Protocol

from quotaframe_bridge.domain.models import Provider, ProviderUsage


class UsageSource(Protocol):
    """Collects one sanitized snapshot for each supported provider."""

    async def collect(self, attempted_at: int | None = None) -> dict[Provider, ProviderUsage]:
        """Return current sanitized usage."""
