"""The opt-in HKCU Run entry, off unless the user ticks the menu item."""

from __future__ import annotations

import sys
import winreg
from pathlib import Path

RUN_KEY = r"Software\Microsoft\Windows\CurrentVersion\Run"
VALUE_NAME = "QuotaFrameBridge"
AUTOSTART_FLAG = "--autostarted"


def current_executable() -> Path:
    """Return the executable the Run entry should point at."""

    return Path(sys.executable)


def _command(executable: Path) -> str:
    return f'"{executable}" {AUTOSTART_FLAG}'


def is_enabled(
    *,
    executable: Path | None = None,
    key_path: str = RUN_KEY,
    value_name: str = VALUE_NAME,
) -> bool:
    """Report whether the Run entry points at this exact executable.

    A value left behind by a copy that has since moved counts as disabled,
    because launching it would start the wrong binary.
    """

    target = current_executable() if executable is None else executable
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, key_path) as key:
            stored, _kind = winreg.QueryValueEx(key, value_name)
    except OSError:
        return False
    return stored == _command(target)


def enable(
    *,
    executable: Path | None = None,
    key_path: str = RUN_KEY,
    value_name: str = VALUE_NAME,
) -> None:
    """Create or overwrite the Run entry."""

    target = current_executable() if executable is None else executable
    with winreg.CreateKey(winreg.HKEY_CURRENT_USER, key_path) as key:
        winreg.SetValueEx(key, value_name, 0, winreg.REG_SZ, _command(target))


def disable(
    *,
    key_path: str = RUN_KEY,
    value_name: str = VALUE_NAME,
) -> None:
    """Remove the Run entry, tolerating an entry that is already gone."""

    try:
        with winreg.OpenKey(
            winreg.HKEY_CURRENT_USER,
            key_path,
            0,
            winreg.KEY_SET_VALUE,
        ) as key:
            winreg.DeleteValue(key, value_name)
    except OSError:
        return
