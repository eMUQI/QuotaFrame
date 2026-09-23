"""Win-CodexBar CLI adapter and the shared CLI resolution machinery.

Only the documented usage windows and timestamps cross this boundary. Raw
stdout, stderr, identity, plan, cost, and source metadata are never retained.

The two upstreams ship incompatible JSON contracts, so discovery, identity
probing, capability probing, and caching are shared through `CliDialect` while
each platform keeps its own paths, argv, and parser. The macOS dialect lives in
`sources.codexbar_mac`.
"""

from __future__ import annotations

import asyncio
import json
import math
import os
import re
import shutil
import subprocess
import sys
import time
from collections.abc import Awaitable, Callable, Mapping, Sequence
from dataclasses import dataclass
from datetime import datetime, timezone
from decimal import Decimal, InvalidOperation, ROUND_HALF_UP
from pathlib import Path
from typing import Any, Literal

from quotaframe_bridge.config import read_codexbar_cli
from quotaframe_bridge.sources.codexbar_dependency import (
    DependencyDownloadError, install_cli, verified_cli,
)
from quotaframe_bridge.domain.models import (
    Provider,
    ProviderUsage,
    SourceState,
    UsageWindow,
)

ProcessRunner = Callable[[tuple[str, ...], float], Awaitable[tuple[int, str, str]]]
UsageParser = Callable[[str, int], "dict[Provider, ProviderUsage]"]
InstalledPaths = Callable[[Mapping[str, str]], "Sequence[Path]"]


class SourceDataError(ValueError):
    """The CodexBar CLI returned data outside the supported contract."""


_PATH_CANDIDATE_NAMES = (
    "codexbar-cli.exe",
    "codexbar-cli",
    "codexbar.exe",
    "codexbar",
)


@dataclass(frozen=True)
class CliDialect:
    """Everything that differs between one platform's CodexBar CLI and another.

    `install_hint` is user-facing, so it must stay free of paths and identity;
    it only names the product to install and the override flag.
    """

    product: str
    # The platform this dialect belongs to, which also selects the layout of
    # the Bridge's own config file, so discovery never mixes the two.
    platform: str
    install_hint: str
    candidate_names: tuple[str, ...]
    installed_paths: InstalledPaths
    usage_argv: tuple[str, ...]
    identity_pattern: re.Pattern[str]
    parse: UsageParser
    # Upstream CodexBar exits non-zero when any single provider fetch fails,
    # while still printing a complete document; Win-CodexBar does not, so there
    # a non-zero exit stays fatal.
    reports_provider_failure_in_exit_code: bool = False
    # A dialect owns its timeout because the two CLIs have different execution
    # models: Win-CodexBar reads local state, while upstream CodexBar performs
    # live web fetches and browser-cookie imports and can legitimately take tens
    # of seconds. Using the Windows-sized timeout for macOS would misclassify a
    # healthy CLI as a capability failure.
    default_timeout: float = 15.0


def _existing_candidate(candidate: Path) -> Path | None:
    try:
        if not candidate.is_file():
            return None
        return candidate.resolve()
    except OSError:
        return None


def _configured_candidate(
    environ: Mapping[str, str], platform: str
) -> Path | None:
    return read_codexbar_cli(environ, platform=platform)


def _windows_installed_paths(environ: Mapping[str, str]) -> tuple[Path, ...]:
    local_app_data = environ.get("LOCALAPPDATA")
    if not local_app_data:
        return ()
    installed = Path(local_app_data) / "Programs" / "CodexBar" / "codexbar-cli.exe"
    managed = verified_cli(environ)
    return (installed,) if managed is None else (installed, managed)


def resolve_codexbar_candidates(
    explicit: Path | None,
    *,
    environ: Mapping[str, str] | None = None,
    which: Callable[[str], str | None] = shutil.which,
    dialect: CliDialect,
) -> tuple[Path, ...]:
    """Return existing CodexBar candidates in precedence order."""

    env = os.environ if environ is None else environ
    if explicit is not None:
        candidate = _existing_candidate(explicit.expanduser())
        return () if candidate is None else (candidate,)

    discovered: list[Path] = []
    seen: set[Path] = set()

    def add(candidate: Path | None) -> None:
        resolved = None if candidate is None else _existing_candidate(candidate)
        if resolved is not None and resolved not in seen:
            seen.add(resolved)
            discovered.append(resolved)

    add(_configured_candidate(env, dialect.platform))

    for installed in dialect.installed_paths(env):
        add(installed)

    for name in dialect.candidate_names:
        located = which(name)
        if located:
            add(Path(located))

    return tuple(discovered)


def resolve_codexbar_cli(
    explicit: Path | None,
    *,
    environ: Mapping[str, str] | None = None,
    which: Callable[[str], str | None] = shutil.which,
    dialect: CliDialect,
) -> Path:
    """Resolve the current CodexBar CLI without inspecting its data stores."""

    candidates = resolve_codexbar_candidates(
        explicit,
        environ=environ,
        which=which,
        dialect=dialect,
    )
    if candidates:
        return candidates[0]
    if explicit is not None:
        candidate = explicit.expanduser()
        raise FileNotFoundError(f"codexbar-cli not found: {candidate}")

    raise FileNotFoundError(f"codexbar-cli was not found; {dialect.install_hint}")


def _reject_constant(value: str) -> None:
    raise SourceDataError(f"non-finite JSON number is not supported: {value}")


def _mapping(value: Any, field: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise SourceDataError(f"{field} must be an object")
    return value


def _timestamp(value: Any, field: str) -> int:
    if not isinstance(value, str):
        raise SourceDataError(f"{field} must be an RFC3339 string")
    normalized = value[:-1] + "+00:00" if value.endswith("Z") else value
    try:
        parsed = datetime.fromisoformat(normalized)
    except ValueError as exc:
        raise SourceDataError(f"{field} is not valid RFC3339") from exc
    if parsed.tzinfo is None:
        raise SourceDataError(f"{field} must include a UTC offset")
    seconds = int(parsed.astimezone(timezone.utc).timestamp())
    if not 0 <= seconds <= 0xFFFFFFFF:
        raise SourceDataError(f"{field} is outside protocol timestamp range")
    return seconds


def _percent(value: Any, field: str) -> int:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise SourceDataError(f"{field} must be numeric")
    if not math.isfinite(float(value)) or not 0 <= float(value) <= 100:
        raise SourceDataError(f"{field} must be finite and between 0 and 100")
    try:
        return int(Decimal(str(value)).quantize(Decimal("1"), rounding=ROUND_HALF_UP))
    except (InvalidOperation, ValueError) as exc:
        raise SourceDataError(f"{field} is invalid") from exc


def _window(value: Any, field: str) -> UsageWindow | None:
    if value is None:
        return None
    raw = _mapping(value, field)
    if raw.get("is_informational") is True:
        return None
    used = _percent(raw.get("used_percent"), f"{field}.used_percent")
    reset_raw = raw.get("resets_at")
    reset = _timestamp(reset_raw, f"{field}.resets_at") if reset_raw is not None else None
    return UsageWindow(used_percent=used, reset_at=reset)


def _unavailable(provider: Provider, attempted_at: int) -> ProviderUsage:
    return ProviderUsage(
        provider=provider,
        state=SourceState.UNAVAILABLE,
        sampled_at=attempted_at,
    )


def parse_codexbar_json(
    text: str, attempted_at: int
) -> dict[Provider, ProviderUsage]:
    """Parse current CLI JSON while projecting it onto the privacy allowlist."""

    try:
        decoded = json.loads(text, parse_constant=_reject_constant)
    except (json.JSONDecodeError, UnicodeError) as exc:
        raise SourceDataError("Win-CodexBar output is not valid JSON") from exc
    if not isinstance(decoded, list):
        raise SourceDataError("top-level JSON must be an array")

    supported = {provider.value: provider for provider in Provider}
    records: dict[Provider, dict[str, Any]] = {}
    for index, item in enumerate(decoded):
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
        if raw is None or "error" in raw:
            output[provider] = _unavailable(provider, attempted_at)
            continue

        usage = _mapping(raw.get("usage"), f"{provider.value}.usage")
        short = _window(usage.get("primary"), f"{provider.value}.usage.primary")
        week = _window(usage.get("secondary"), f"{provider.value}.usage.secondary")
        sampled_at = _timestamp(
            usage.get("updated_at"), f"{provider.value}.usage.updated_at"
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


_NO_WINDOW = subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0


async def _run_process(
    argv: tuple[str, ...], timeout: float
) -> tuple[int, str, str]:
    environment = None
    if sys.platform == "win32":
        environment = dict(os.environ)
        # The standalone CLI needs the VC runtime bundled with CPython/PyInstaller.
        runtime = str(getattr(sys, "_MEIPASS", sys.base_prefix))
        environment["PATH"] = runtime + os.pathsep + environment.get("PATH", "")
    process = await asyncio.create_subprocess_exec(
        *argv,
        stdout=asyncio.subprocess.PIPE,
        stderr=asyncio.subprocess.PIPE,
        creationflags=_NO_WINDOW,
        env=environment,
    )
    try:
        stdout, stderr = await asyncio.wait_for(process.communicate(), timeout)
    except (TimeoutError, asyncio.CancelledError) as exc:
        if process.returncode is None:
            try:
                process.kill()
            except ProcessLookupError:
                pass
        await process.communicate()
        if isinstance(exc, asyncio.CancelledError):
            raise
        raise RuntimeError("Win-CodexBar timed out") from None
    return (
        int(process.returncode or 0),
        stdout.decode("utf-8", errors="strict"),
        stderr.decode("utf-8", errors="replace"),
    )


ResolutionCategory = Literal["not_found", "identity", "capability"]


class CodexBarResolutionError(RuntimeError):
    """A sanitized failure while selecting a compatible CodexBar CLI."""

    _MESSAGES = {
        "not_found": "no CodexBar CLI executable was found; {hint}",
        "identity": (
            "found executable files, but none identified as a compatible "
            "CodexBar CLI"
        ),
        "capability": (
            "found CodexBar CLI executables, but none returned supported "
            "usage data"
        ),
    }
    _DEFAULT_HINT = "install Win-CodexBar or pass --codexbar-cli"

    def __init__(
        self,
        category: ResolutionCategory,
        *,
        install_hint: str | None = None,
    ) -> None:
        self.category = category
        hint = self._DEFAULT_HINT if install_hint is None else install_hint
        super().__init__(self._MESSAGES[category].format(hint=hint))


_CODEXBAR_VERSION = re.compile(r"^codexbar \d+\.\d+\.\d+")
_PROBE_ERRORS = (OSError, RuntimeError, SourceDataError, UnicodeError)

WINDOWS_DIALECT = CliDialect(
    product="Win-CodexBar",
    platform="win32",
    install_hint="install Win-CodexBar or pass --codexbar-cli",
    candidate_names=_PATH_CANDIDATE_NAMES,
    installed_paths=_windows_installed_paths,
    usage_argv=("usage", "-p", "both", "--json"),
    identity_pattern=_CODEXBAR_VERSION,
    parse=parse_codexbar_json,
)


class CodexBarResolver:
    """Select, validate, cache, and recover the configured CodexBar CLI."""

    def __init__(
        self,
        explicit: Path | None,
        *,
        environ: Mapping[str, str] | None = None,
        which: Callable[[str], str | None] = shutil.which,
        timeout: float | None = None,
        runner: ProcessRunner = _run_process,
        dialect: CliDialect,
    ) -> None:
        self._explicit = explicit
        self._environ = environ
        self._which = which
        self._runner = runner
        self._dialect = dialect
        self._timeout = self._dialect.default_timeout if timeout is None else timeout
        self._selected: Path | None = None

    async def _run(
        self, executable: Path, arguments: tuple[str, ...]
    ) -> tuple[int, str, str]:
        return await self._runner((str(executable), *arguments), self._timeout)

    async def _usage(
        self, executable: Path, attempted_at: int
    ) -> dict[Provider, ProviderUsage]:
        return_code, stdout, _stderr = await self._run(
            executable,
            self._dialect.usage_argv,
        )
        if return_code != 0 and not self._dialect.reports_provider_failure_in_exit_code:
            raise RuntimeError("CodexBar usage command failed")
        try:
            parsed = self._dialect.parse(stdout, attempted_at)
        except SourceDataError:
            if return_code != 0:
                raise RuntimeError("CodexBar usage command failed") from None
            raise
        if return_code != 0 and not any(
            usage.state is not SourceState.UNAVAILABLE for usage in parsed.values()
        ):
            # Every provider failed, so this candidate proved nothing about its
            # own health; let the resolver keep probing instead of caching it.
            raise RuntimeError("CodexBar usage command failed")
        return parsed

    async def _has_cli_identity(self, executable: Path) -> bool:
        try:
            return_code, stdout, _stderr = await self._run(
                executable,
                ("--version",),
            )
        except _PROBE_ERRORS:
            return False
        return (
            return_code == 0
            and self._dialect.identity_pattern.match(stdout) is not None
        )

    async def collect(
        self, attempted_at: int | None = None
    ) -> dict[Provider, ProviderUsage]:
        now = int(time.time()) if attempted_at is None else attempted_at

        if self._selected is not None:
            try:
                return await self._usage(self._selected, now)
            except _PROBE_ERRORS:
                self._selected = None

        candidates = resolve_codexbar_candidates(
            self._explicit,
            environ=self._environ,
            which=self._which,
            dialect=self._dialect,
        )
        hint = self._dialect.install_hint
        if not candidates:
            raise CodexBarResolutionError("not_found", install_hint=hint)

        identity_valid = False
        for candidate in candidates:
            if not await self._has_cli_identity(candidate):
                continue
            identity_valid = True
            try:
                result = await self._usage(candidate, now)
            except _PROBE_ERRORS:
                continue
            self._selected = candidate
            return result

        if not identity_valid:
            raise CodexBarResolutionError("identity", install_hint=hint)
        raise CodexBarResolutionError("capability", install_hint=hint)


class WinCodexBarSource:
    """Collect usage by executing Win-CodexBar's current structured CLI."""

    def __init__(
        self,
        executable: Path | None = None,
        *,
        timeout: float | None = None,
        runner: ProcessRunner = _run_process,
        environ: Mapping[str, str] | None = None,
        which: Callable[[str], str | None] = shutil.which,
        dependency_status: Callable[[str], None] | None = None,
    ) -> None:
        self._executable = executable
        self._environ = os.environ if environ is None else environ
        self._dependency_status = dependency_status
        self._download_retry_at = 0.0
        self._resolver = CodexBarResolver(
            executable,
            environ=environ,
            which=which,
            timeout=timeout,
            runner=runner,
            dialect=WINDOWS_DIALECT,
        )

    async def collect(
        self, attempted_at: int | None = None
    ) -> dict[Provider, ProviderUsage]:
        try:
            return await self._resolver.collect(attempted_at)
        except CodexBarResolutionError as exc:
            if (
                exc.category == "capability"
                or self._executable is not None
                or read_codexbar_cli(self._environ, platform="win32") is not None
                or not self._environ.get("LOCALAPPDATA")
                or time.monotonic() < self._download_retry_at
            ):
                raise
        self._download_retry_at = time.monotonic() + 300
        if self._dependency_status is not None:
            self._dependency_status("downloading")
        try:
            await asyncio.to_thread(install_cli, self._environ)
        except DependencyDownloadError:
            if self._dependency_status is not None:
                self._dependency_status("failed")
            raise
        if self._dependency_status is not None:
            self._dependency_status("ready")
        return await self._resolver.collect(attempted_at)
