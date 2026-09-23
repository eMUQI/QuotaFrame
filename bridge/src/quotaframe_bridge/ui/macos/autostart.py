"""The opt-in login item, off unless the user ticks the menu item.

macOS 13 added `SMAppService`, but it registers a *bundle* and the Bridge is
unbundled. A per-user LaunchAgent is used instead: it works unbundled and is a
plain file this module can write and verify without any private API.

Mirrors `ui/autostart.py`: same `is_enabled` / `enable` / `disable` shape, same
rule that an entry pointing somewhere else counts as disabled.
"""

from __future__ import annotations

import os
import plistlib
import sys
from collections.abc import Mapping
from pathlib import Path

LABEL = "com.quotaframe.bridge"
AUTOSTART_FLAG = "--autostarted"
LAUNCH_AGENTS = ("Library", "LaunchAgents")


def current_executable() -> Path:
    return Path(sys.executable)


def agent_path(
    environ: Mapping[str, str] | None = None,
    *,
    label: str = LABEL,
) -> Path:
    """Return the LaunchAgent plist path for the current user."""

    source = os.environ if environ is None else environ
    home = source.get("HOME")
    root = Path(home) if home else Path.home()
    return root.joinpath(*LAUNCH_AGENTS) / f"{label}.plist"


def _program_arguments(executable: Path) -> list[str]:
    if getattr(sys, "frozen", False):
        return [str(executable), AUTOSTART_FLAG]
    return [str(executable), "-m", "quotaframe_bridge.ui.macos.app", AUTOSTART_FLAG]


def is_enabled(
    *,
    executable: Path | None = None,
    environ: Mapping[str, str] | None = None,
    label: str = LABEL,
) -> bool:
    """Report whether a login item points at this exact executable.

    An agent left behind by an interpreter that has since moved counts as
    disabled: launching it would start the wrong Python.
    """

    target = current_executable() if executable is None else executable
    path = agent_path(environ, label=label)
    try:
        stored = plistlib.loads(path.read_bytes())
    except (OSError, plistlib.InvalidFileException, ValueError):
        return False
    return stored.get("ProgramArguments") == _program_arguments(target)


def enable(
    *,
    executable: Path | None = None,
    environ: Mapping[str, str] | None = None,
    label: str = LABEL,
) -> None:
    """Create or overwrite the LaunchAgent."""

    target = current_executable() if executable is None else executable
    path = agent_path(environ, label=label)
    path.parent.mkdir(parents=True, exist_ok=True)
    document = {
        "Label": label,
        "ProgramArguments": _program_arguments(target),
        "RunAtLoad": True,
        # launchd would otherwise restart the Bridge every time the user quits
        # it from the menu, which turns the Quit item into a no-op.
        "KeepAlive": False,
        "ProcessType": "Interactive",
    }
    path.write_bytes(plistlib.dumps(document))


def disable(
    *,
    environ: Mapping[str, str] | None = None,
    label: str = LABEL,
) -> None:
    """Remove the LaunchAgent, tolerating one that is already gone."""

    try:
        agent_path(environ, label=label).unlink()
    except OSError:
        return
