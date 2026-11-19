"""Source adapter contract."""

from __future__ import annotations

from collections.abc import Callable
from typing import Protocol, runtime_checkable

from quotaframe_bridge.domain.models import Provider, ProviderUsage

UsageUpdate = Callable[[ProviderUsage], None]


class UsageSource(Protocol):
    """Collects one sanitized snapshot for each supported provider."""

    async def collect(self, attempted_at: int | None = None) -> dict[Provider, ProviderUsage]:
        """Return current sanitized usage."""


@runtime_checkable
class ProgressiveUsageSource(UsageSource, Protocol):
    """A source that can report a provider while the collection is pending."""

    async def collect_progressively(
        self, attempted_at: int | None, *, on_update: UsageUpdate,
    ) -> dict[Provider, ProviderUsage]:
        """Report early provider results and return the complete collection."""
