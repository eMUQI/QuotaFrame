"""Upstream (macOS) CodexBar CLI adapter.

The Swift upstream and the Rust Windows port do not share a JSON contract, so
this module owns the camelCase dialect and reuses the discovery, probing, and
caching machinery in `sources.codexbar`.

Contract confirmed against upstream source, not only `docs/cli.md`:

- `usage --format json` prints provider payloads as a JSON **array**
  (`CLIUsageCommand.printJSON` receives `[ProviderPayload]`). The single-object
  sample in `docs/cli.md` is a simplification, so a bare object is accepted too.
- `--provider both` asks upstream for its primary providers. The parser remains
  limited to this project's supported providers, Codex and Claude, and ignores
  any unrelated provider records rather than widening the protocol surface.
- `RateWindow` carries `usedPercent`, `windowMinutes`, `resetsAt`, and
  `isSyntheticPlaceholder`; the last one marks a lane the provider never
  actually reported (Claude web can report a phantom 0% session window when the
  account has no live session), so it must read as "no window", not as 0%.
- `usedPercent` is deliberately not clamped upstream: over-quota lanes can
  exceed 100. The panel is a display, so it clamps instead of rejecting.
- Dates are encoded with `JSONEncoder.dateEncodingStrategy = .iso8601`.
- The command exits non-zero when *any* provider fetch fails while still
  printing the full document, so the exit code alone cannot condemn the CLI.

Only the same allowlist as the Windows adapter crosses this boundary: two
window percentages, their reset timestamps, and the sample time. `identity`,
`accountEmail`, `pace`, `credits`, `openaiDashboard`, and `error` details are
read past, never retained.
"""

from __future__ import annotations

import json
import math
import re
import shutil
from collections.abc import Callable, Mapping
from decimal import Decimal, InvalidOperation, ROUND_HALF_UP
from pathlib import Path
from typing import Any

from quotaframe_bridge.domain.models import (
    Provider,
    ProviderUsage,
    SourceState,
    UsageWindow,
)
from quotaframe_bridge.sources.codexbar import (
    CliDialect,
    CodexBarResolver,
    ProcessRunner,
    SourceDataError,
    _mapping,
    _reject_constant,
    _run_process,
    _timestamp,
    _unavailable,
)

APPLICATION_HELPER = ("Contents", "Helpers", "CodexBarCLI")
INSTALL_HINT = "install CodexBar and its CLI, or pass --codexbar-cli"

# Upstream performs live web fetches and browser cookie imports rather than
# reading local state, so collection can take tens of seconds on a healthy
# account. Keep a substantially larger timeout than the local-state Windows
# adapter without allowing a wedged fetch to stall collection indefinitely.
MACOS_USAGE_TIMEOUT = 90.0

_PATH_CANDIDATE_NAMES = ("codexbar", "CodexBarCLI")
# `codexbar --version` prints "CodexBar <semver>", or a bare "CodexBar" when the
# bundle version cannot be read.
_CODEXBAR_VERSION = re.compile(r"^CodexBar\b", re.IGNORECASE)


def installed_paths(environ: Mapping[str, str]) -> tuple[Path, ...]:
    """Return the documented install locations, in precedence order."""

    candidates = [
        Path("/opt/homebrew/bin/codexbar"),
        Path("/usr/local/bin/codexbar"),
        Path("/Applications/CodexBar.app").joinpath(*APPLICATION_HELPER),
    ]
    home = environ.get("HOME")
    if home:
        bundle = Path(home) / "Applications" / "CodexBar.app"
        candidates.append(bundle.joinpath(*APPLICATION_HELPER))
    return tuple(candidates)


def _display_percent(value: Any, field: str) -> int:
    """Round a percentage for display, clamping the upstream over-quota range."""

    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise SourceDataError(f"{field} must be numeric")
    number = float(value)
    if not math.isfinite(number):
        raise SourceDataError(f"{field} must be finite")
    bounded = min(100.0, max(0.0, number))
    try:
        return int(Decimal(str(bounded)).quantize(Decimal("1"), rounding=ROUND_HALF_UP))
    except (InvalidOperation, ValueError) as exc:
        raise SourceDataError(f"{field} is invalid") from exc


def _window(value: Any, field: str) -> UsageWindow | None:
    if value is None:
        return None
    raw = _mapping(value, field)
    if raw.get("isSyntheticPlaceholder") is True:
        return None
    used = _display_percent(raw.get("usedPercent"), f"{field}.usedPercent")
    reset_raw = raw.get("resetsAt")
    reset = _timestamp(reset_raw, f"{field}.resetsAt") if reset_raw is not None else None
    return UsageWindow(used_percent=used, reset_at=reset)


def _records(decoded: Any) -> list[Any]:
    if isinstance(decoded, list):
        return decoded
    if isinstance(decoded, dict):
        return [decoded]
    raise SourceDataError("top-level JSON must be an array or an object")


def parse_codexbar_mac_json(
    text: str, attempted_at: int
) -> dict[Provider, ProviderUsage]:
    """Parse upstream CodexBar JSON onto the same privacy allowlist."""

    try:
        decoded = json.loads(text, parse_constant=_reject_constant)
    except (json.JSONDecodeError, UnicodeError) as exc:
        raise SourceDataError("CodexBar output is not valid JSON") from exc

    supported = {provider.value: provider for provider in Provider}
    records: dict[Provider, dict[str, Any]] = {}
    for index, item in enumerate(_records(decoded)):
        raw = _mapping(item, f"result[{index}]")
        provider_name = raw.get("provider")
        if provider_name not in supported:
            continue
        provider = supported[provider_name]
        if provider in records:
            raise SourceDataError(f"duplicate provider result: {provider.value}")
        records[provider] = raw

    output: dict[Provider, ProviderUsage] = {}
    for provider in Provider:
        raw = records.get(provider)
        if raw is None or raw.get("error") is not None:
            output[provider] = _unavailable(provider, attempted_at)
            continue

        usage_raw = raw.get("usage")
        if usage_raw is None:
            output[provider] = _unavailable(provider, attempted_at)
            continue
        usage = _mapping(usage_raw, f"{provider.value}.usage")
        short = _window(usage.get("primary"), f"{provider.value}.usage.primary")
        week = _window(usage.get("secondary"), f"{provider.value}.usage.secondary")
        sampled_at = _timestamp(
            usage.get("updatedAt"), f"{provider.value}.usage.updatedAt"
        )
        present = int(short is not None) + int(week is not None)
        state = (
            SourceState.OK
            if present == 2
            else SourceState.PARTIAL
            if present == 1
            else SourceState.UNAVAILABLE
        )
        output[provider] = ProviderUsage(
            provider=provider,
            state=state,
            sampled_at=sampled_at,
            short=short,
            week=week,
        )
    return output


MACOS_DIALECT = CliDialect(
    product="CodexBar",
    platform="darwin",
    install_hint=INSTALL_HINT,
    candidate_names=_PATH_CANDIDATE_NAMES,
    installed_paths=installed_paths,
    usage_argv=("usage", "--provider", "both", "--format", "json"),
    identity_pattern=_CODEXBAR_VERSION,
    parse=parse_codexbar_mac_json,
    reports_provider_failure_in_exit_code=True,
    default_timeout=MACOS_USAGE_TIMEOUT,
)


class MacCodexBarSource:
    """Collect usage by executing upstream CodexBar's structured CLI."""

    def __init__(
        self,
        executable: Path | None = None,
        *,
        timeout: float | None = None,
        runner: ProcessRunner = _run_process,
        environ: Mapping[str, str] | None = None,
        which: Callable[[str], str | None] = shutil.which,
    ) -> None:
        self._resolver = CodexBarResolver(
            executable,
            environ=environ,
            which=which,
            timeout=timeout,
            runner=runner,
            dialect=MACOS_DIALECT,
        )

    async def collect(
        self, attempted_at: int | None = None
    ) -> dict[Provider, ProviderUsage]:
        return await self._resolver.collect(attempted_at)
