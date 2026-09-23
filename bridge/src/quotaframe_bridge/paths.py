"""Per-platform locations for the Bridge's own files.

Windows keeps the roaming/local split it always had. macOS follows the Apple
convention instead of reusing the Windows variables, so a user with both hosts
never has one platform's Bridge writing into the other's directory layout.
"""

from __future__ import annotations

import sys
import tempfile
from collections.abc import Mapping
from pathlib import Path

CONFIG_DIRECTORY = "quotaframe"
DATA_DIRECTORY = "quotaframe"

MACOS_APPLICATION_SUPPORT = ("Library", "Application Support")
MACOS_LOGS = ("Library", "Logs")


def tray_assets() -> Path:
    """Return the directory holding tray and menu bar artwork.

    PyInstaller unpacks the bundled copy under `sys._MEIPASS`. A checkout
    keeps it at the repository root, three levels above this package: the
    package directory, `src`, then `bridge`.
    """

    bundled = getattr(sys, "_MEIPASS", None)
    if bundled is not None:
        return Path(bundled) / "tray"
    return Path(__file__).resolve().parents[3] / "assets" / "tray"


def _home(environ: Mapping[str, str]) -> Path | None:
    home = environ.get("HOME")
    return Path(home) if home else None


def config_directory(
    environ: Mapping[str, str],
    *,
    platform: str = sys.platform,
) -> Path | None:
    """Return the directory holding config.toml, or None when undiscoverable."""

    if platform == "win32":
        app_data = environ.get("APPDATA")
        return Path(app_data) / CONFIG_DIRECTORY if app_data else None
    home = _home(environ)
    if home is None:
        return None
    return home.joinpath(*MACOS_APPLICATION_SUPPORT) / CONFIG_DIRECTORY


def data_directory(
    environ: Mapping[str, str],
    *,
    platform: str = sys.platform,
) -> Path:
    """Return the directory for Bridge state such as the instance lock.

    This one falls back to a temporary directory rather than returning None:
    the instance lock has to land somewhere, and a lock in the temp directory
    still guards a single login session.
    """

    if platform == "win32":
        base = environ.get("LOCALAPPDATA")
        root = Path(base) if base else Path(tempfile.gettempdir())
        return root / DATA_DIRECTORY
    home = _home(environ)
    if home is None:
        return Path(tempfile.gettempdir()) / DATA_DIRECTORY
    return home.joinpath(*MACOS_APPLICATION_SUPPORT) / DATA_DIRECTORY


def log_directory(
    environ: Mapping[str, str],
    *,
    platform: str = sys.platform,
) -> Path:
    """Return the directory for the rotating Bridge log."""

    if platform == "win32":
        return data_directory(environ, platform=platform)
    home = _home(environ)
    if home is None:
        return data_directory(environ, platform=platform)
    return home.joinpath(*MACOS_LOGS) / DATA_DIRECTORY
