"""In-memory sanitized state store shared by CLI and UI adapters."""

from __future__ import annotations

from collections.abc import Mapping
from types import MappingProxyType

from quotaframe_bridge.domain.models import Provider, ProviderUsage, SourceState


class UsageStateStore:
    """Retain latest publish state and last valid provider values separately."""

    def __init__(self) -> None:
        self._providers: dict[Provider, ProviderUsage] = {}
        self._last_valid: dict[Provider, ProviderUsage] = {}
        self.last_collection_attempt: int | None = None

    @property
    def providers(self) -> Mapping[Provider, ProviderUsage]:
        return MappingProxyType(self._providers)

    @property
    def last_valid(self) -> Mapping[Provider, ProviderUsage]:
        return MappingProxyType(self._last_valid)

    def merge_collection(
        self,
        collection: Mapping[Provider, ProviderUsage],
        *,
        attempted_at: int,
    ) -> Mapping[Provider, ProviderUsage]:
        self.last_collection_attempt = attempted_at
        for provider in Provider:
            usage = collection.get(provider)
            if usage is None:
                usage = ProviderUsage(
                    provider=provider,
                    state=SourceState.UNAVAILABLE,
                    sampled_at=attempted_at,
                )
            if usage.state is not SourceState.UNAVAILABLE:
                self._last_valid[provider] = usage
            else:
                previous = self._last_valid.get(provider)
                if previous is not None:
                    # Preserve source age so retries cannot extend data freshness.
                    usage = previous
            self._providers[provider] = usage
        return self.providers

    def mark_all_unavailable(
        self, *, attempted_at: int
    ) -> Mapping[Provider, ProviderUsage]:
        unavailable = {
            provider: ProviderUsage(
                provider=provider,
                state=SourceState.UNAVAILABLE,
                sampled_at=attempted_at,
            )
            for provider in Provider
        }
        return self.merge_collection(unavailable, attempted_at=attempted_at)
