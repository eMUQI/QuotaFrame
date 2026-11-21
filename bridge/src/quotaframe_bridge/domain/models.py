"""Credential-free usage models shared by Bridge adapters."""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum


class Provider(str, Enum):
    """Usage providers supported by protocol version 1."""

    CODEX = "codex"
    CLAUDE = "claude"
    GEMINI = "gemini"


BASE_PROVIDERS = (Provider.CODEX, Provider.CLAUDE)
DISPLAY_PAIR_CAPABILITY = "display.pair.v1"


class DisplayPair(str, Enum):
    """An ordered pair of visible provider slots."""

    CODEX_CLAUDE = "codex_claude"
    CODEX_GEMINI = "codex_gemini"
    CLAUDE_GEMINI = "claude_gemini"

    @property
    def label(self) -> str:
        return " + ".join(provider.title() for provider in self.value.split("_"))

    @property
    def needs_gemini(self) -> bool:
        return self is not DisplayPair.CODEX_CLAUDE


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
