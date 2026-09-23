"""Process-wide desktop language, resolved once before rendering any copy."""

from __future__ import annotations

import ctypes
import sys
from functools import cache

from quotaframe_bridge.catalog import MESSAGES
from quotaframe_bridge.config import read_language


def system_language() -> str:
    """Read the user's primary UI language, independently of regional formats."""

    try:
        if sys.platform == "darwin":
            from Foundation import NSLocale

            languages = NSLocale.preferredLanguages()
            if languages:
                primary = str(languages[0]).replace("_", "-").split("-")[0]
                return "zh" if primary.lower() == "zh" else "en"
        elif sys.platform == "win32":
            get_language = ctypes.windll.kernel32.GetUserDefaultUILanguage
            get_language.restype = ctypes.c_ushort
            get_language.argtypes = []
            language_id = get_language()
            return "zh" if language_id & 0x3FF == 0x04 else "en"
    except (ImportError, AttributeError, OSError, TypeError, ValueError):
        pass
    return "en"


@cache
def language() -> str:
    """Keep menus, dialogs and worker notifications in one startup language."""

    configured = read_language()
    return system_language() if configured == "auto" else configured


def tr(key: str, **values: object) -> str:
    """Render a catalog entry, preserving templates when no values are supplied."""

    text = MESSAGES[key][0 if language() == "zh" else 1]
    return text.format(**values) if values else text
