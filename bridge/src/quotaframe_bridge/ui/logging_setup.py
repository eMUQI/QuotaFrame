"""Rotating file logging shared by the Windows and macOS tray entry points.

Both tray applications log to the same file, with the same rotation and the
same startup banner. Only the entry points call this; the CLI leaves logging
to its caller.
"""

from __future__ import annotations

import logging
import os
import sys
from logging.handlers import RotatingFileHandler
from pathlib import Path

from quotaframe_bridge.config import read_log_level
from quotaframe_bridge.paths import log_directory

LOGGER = logging.getLogger(__name__)

LOG_FILENAME = "bridge.log"
LOG_BYTES = 1_048_576
LOG_BACKUPS = 3


def log_path() -> Path:
    return log_directory(os.environ) / LOG_FILENAME


def configure_logging() -> Path:
    path = log_path()
    path.parent.mkdir(parents=True, exist_ok=True)
    handler = RotatingFileHandler(
        path,
        maxBytes=LOG_BYTES,
        backupCount=LOG_BACKUPS,
        encoding="utf-8",
    )
    handler.setFormatter(
        logging.Formatter("%(asctime)s %(levelname)s %(name)s: %(message)s")
    )
    root = logging.getLogger()
    root.setLevel(getattr(logging, read_log_level()))
    root.addHandler(handler)
    LOGGER.info(
        "quotaframe-bridge starting; executable=%s log_level=%s",
        sys.executable,
        read_log_level(),
    )
    return path
