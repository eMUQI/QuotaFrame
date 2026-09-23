"""Credential-free usage models shared by Bridge adapters."""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum


class Provider(str, Enum):
    """Usage providers supported by protocol version 1."""

    CODEX = "codex"
    CLAUDE = "claude"


class SourceState(str, Enum):
    """Whether a provider sample contains all, some, or no usage windows."""

    OK = "ok"
    PARTIAL = "partial"
    UNAVAILABLE = "unavailable"


@dataclass(frozen=True, slots=True)
class UsageWindow:
    """One normalized rate window."""

    used_percent: int
    reset_at: int | None = None

    def __post_init__(self) -> None:
        if not 0 <= self.used_percent <= 100:
            raise ValueError("used_percent must be between 0 and 100")
        if self.reset_at is not None and not 0 <= self.reset_at <= 0xFFFFFFFF:
            raise ValueError("reset_at must fit an unsigned 32-bit timestamp")


@dataclass(frozen=True, slots=True)
class ProviderUsage:
    """Sanitized provider state; deliberately contains no identity metadata."""

    provider: Provider
    state: SourceState
    sampled_at: int
    short: UsageWindow | None = None
    week: UsageWindow | None = None

    def __post_init__(self) -> None:
        if not 0 <= self.sampled_at <= 0xFFFFFFFF:
            raise ValueError("sampled_at must fit an unsigned 32-bit timestamp")
        present = int(self.short is not None) + int(self.week is not None)
        expected = (
            SourceState.OK
            if present == 2
            else SourceState.PARTIAL
            if present == 1
            else SourceState.UNAVAILABLE
        )
        if self.state is not expected:
            raise ValueError(f"{self.state.value} does not match available windows")
