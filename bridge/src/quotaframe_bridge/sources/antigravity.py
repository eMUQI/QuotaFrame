"""Collect the Antigravity CLI report for the ordinary Gemini quota group."""

from __future__ import annotations

import json
import os
import re
import shutil
import sys
from datetime import datetime
from decimal import ROUND_HALF_UP, Decimal
from pathlib import Path
from tempfile import TemporaryDirectory

from quotaframe_bridge.domain.models import Provider, ProviderUsage, SourceState, UsageWindow
from quotaframe_bridge.sources.codexbar import _run_process

_MIN_VERSION = (1, 1, 11)
_BUCKET_IDS = ("gemini-5h", "gemini-weekly")


def _reset_time(value: object) -> int | None:
    if not isinstance(value, str):
        return None
    try:
        reset = datetime.fromisoformat(value)
        if reset.tzinfo is None:
            return None
        epoch = int(reset.timestamp())
    except (ValueError, OverflowError, OSError):
        return None
    return epoch if 0 <= epoch <= 0xFFFFFFFF else None


def parse_gemini_report(output: str, attempted_at: int) -> ProviderUsage:
    """Read measured Gemini windows from a successful built-in usage command."""
    report = json.loads(output, parse_float=Decimal)
    command = report.get("command") if isinstance(report, dict) else None
    if (not isinstance(command, dict) or command.get("name") != "usage"
            or report.get("status") != "SUCCESS"
            or type(report.get("num_turns")) is not int or report["num_turns"] != 0):
        raise ValueError("Antigravity did not return a successful usage report")
    data = command.get("data")
    groups = data.get("groups") if isinstance(data, dict) else None
    if not isinstance(groups, list):
        raise ValueError("Antigravity usage groups are missing")

    windows: dict[str, UsageWindow] = {}
    for group in groups:
        buckets = group.get("buckets") if isinstance(group, dict) else None
        if not isinstance(buckets, list):
            continue
        for bucket in buckets:
            key = bucket.get("id") if isinstance(bucket, dict) else None
            if key not in _BUCKET_IDS or bucket.get("disabled") is True:
                continue
            remaining = bucket.get("remaining_fraction")
            if type(remaining) not in (int, Decimal) or not 0 <= remaining <= 1:
                continue
            if key in windows:
                raise ValueError("Antigravity usage report repeats a Gemini quota bucket")
            used = int(((1 - Decimal(remaining)) * 100).quantize(Decimal(1), rounding=ROUND_HALF_UP))
            windows[key] = UsageWindow(used, _reset_time(bucket.get("reset_time")))

    short, week = (windows.get(key) for key in _BUCKET_IDS)
    state = SourceState.OK if short and week else (
        SourceState.PARTIAL if short or week else SourceState.UNAVAILABLE
    )
    return ProviderUsage(Provider.GEMINI, state, attempted_at, short=short, week=week)


def _cli_path() -> Path | None:
    override = os.environ.get("ANTIGRAVITY_CLI_PATH")
    if override is not None:
        return Path(override).expanduser().resolve() if override else None
    executable = shutil.which("agy")
    if executable:
        return Path(executable).resolve()
    if sys.platform == "win32":
        app_data = os.environ.get("LOCALAPPDATA")
        return Path(app_data) / "agy/bin/agy.exe" if app_data else None
    return next((path for path in (
        Path.home() / ".local/bin/agy",
        Path("/opt/homebrew/bin/agy"),
        Path("/usr/local/bin/agy"),
    ) if path.is_file()), None)


async def collect_gemini(attempted_at: int) -> ProviderUsage:
    """Read usage using the user's installed and previously signed-in CLI."""
    path = _cli_path()
    if path is None or not path.is_file():
        raise FileNotFoundError("Antigravity CLI is not installed")
    with TemporaryDirectory(prefix="quotaframe-agy-") as cwd:
        environment = dict(os.environ, AGY_CLI_DISABLE_AUTO_UPDATE="true")
        if sys.platform != "win32":
            launcher = "#!/bin/sh\nexit 1\n"
            for name in ("open", "xdg-open"):
                shim = Path(cwd) / name
                shim.write_text(launcher, encoding="utf-8")
                shim.chmod(0o700)
            environment["BROWSER"] = str(Path(cwd) / "open")
            environment["PATH"] = cwd + os.pathsep + environment.get("PATH", "")
        argv = (str(path),)
        status, version, _ = await _run_process(
            (*argv, "--version"), 10, environment=environment, cwd=cwd,
        )
        match = re.search(r"(?<![\d.])(\d+)\.(\d+)\.(\d+)(?![\d.])", version)
        if status or match is None or tuple(map(int, match.groups())) < _MIN_VERSION:
            raise ValueError("Antigravity CLI v1.1.11 or later is required")
        status, report, _ = await _run_process(
            (*argv, "-p", "/usage", "--output-format", "json"), 90,
            environment=environment, cwd=cwd,
        )
        if status:
            raise ValueError("Antigravity usage command failed")
        return parse_gemini_report(report, attempted_at)
