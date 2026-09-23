"""Identify whether this executable belongs to the current-user installation."""

from __future__ import annotations

import sys
import winreg
from pathlib import Path

UNINSTALL_KEY = r"Software\Microsoft\Windows\CurrentVersion\Uninstall\QuotaFrame.Bridge_is1"


def is_installed() -> bool | None:
    """Return None for source runs or an unreadable installation record."""
    if not getattr(sys, "frozen", False):
        return None
    try:
        key = winreg.OpenKey(
            winreg.HKEY_CURRENT_USER, UNINSTALL_KEY, 0,
            winreg.KEY_READ | winreg.KEY_WOW64_64KEY,
        )
    except FileNotFoundError:
        return False
    except OSError:
        return None
    try:
        with key:
            location, kind = winreg.QueryValueEx(key, "InstallLocation")
        if kind != winreg.REG_SZ or not isinstance(location, str) or not location:
            return None
        directory = Path(location)
        if not directory.is_absolute():
            return None
        return Path(sys.executable).resolve() == (directory / "quotaframe-bridge.exe").resolve()
    except OSError:
        return None
