"""Single reader for the user-local Bridge configuration file."""

from __future__ import annotations

import os
import sys
import tomllib
from collections.abc import Mapping
from pathlib import Path
from typing import Any

from quotaframe_bridge.paths import CONFIG_DIRECTORY, config_directory

CONFIG_FILENAME = "config.toml"
VALID_LOG_LEVELS = ("DEBUG", "INFO", "WARNING", "ERROR")
DEFAULT_LOG_LEVEL = "INFO"

__all__ = [
    "CONFIG_DIRECTORY",
    "CONFIG_FILENAME",
    "DEFAULT_LOG_LEVEL",
    "VALID_LOG_LEVELS",
    "config_path",
    "load_config",
    "read_codexbar_cli",
    "read_log_level",
    "read_language",
]


def config_path(
    environ: Mapping[str, str] | None = None,
    *,
    platform: str = sys.platform,
) -> Path | None:
    """Return the configuration file path, or None when its root is unset."""

    source = os.environ if environ is None else environ
    directory = config_directory(source, platform=platform)
    if directory is None:
        return None
    return directory / CONFIG_FILENAME


def load_config(
    environ: Mapping[str, str] | None = None,
    *,
    platform: str = sys.platform,
) -> Mapping[str, Any]:
    """Parse the configuration file, returning {} for any unreadable file."""

    path = config_path(environ, platform=platform)
    if path is None:
        return {}
    try:
        return tomllib.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, tomllib.TOMLDecodeError):
        return {}


def read_codexbar_cli(
    environ: Mapping[str, str] | None = None,
    *,
    platform: str = sys.platform,
) -> Path | None:
    """Return the configured CodexBar CLI path, or None when unset."""

    value = load_config(environ, platform=platform).get("codexbar_cli")
    if not isinstance(value, str) or not value.strip():
        return None
    return Path(value).expanduser()


def read_log_level(
    environ: Mapping[str, str] | None = None,
    *,
    platform: str = sys.platform,
) -> str:
    """Return a validated log level name, defaulting to INFO."""

    value = load_config(environ, platform=platform).get("log_level")
    if not isinstance(value, str):
        return DEFAULT_LOG_LEVEL
    candidate = value.strip().upper()
    if candidate not in VALID_LOG_LEVELS:
        return DEFAULT_LOG_LEVEL
    return candidate


def read_language(
    environ: Mapping[str, str] | None = None,
    *,
    platform: str = sys.platform,
) -> str:
    """Return auto, zh or en; missing and invalid settings follow the system."""

    value = load_config(environ, platform=platform).get("language")
    if not isinstance(value, str):
        return "auto"
    candidate = value.strip().lower()
    return candidate if candidate in {"auto", "zh", "en"} else "auto"
