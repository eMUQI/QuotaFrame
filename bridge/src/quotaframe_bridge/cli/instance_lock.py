"""One-process guard for the Bridge command-line entry point.

Both backends lock the open file description rather than the process, so a
second `acquire` inside one process is rejected the same way a second Bridge
process is. That property is what the tests exercise.
"""

from __future__ import annotations

import os
import sys
from collections.abc import Mapping
from pathlib import Path
from typing import BinaryIO

from quotaframe_bridge.paths import data_directory

if sys.platform == "win32":
    import msvcrt
else:
    import fcntl

LOCK_FILENAME = "bridge.lock"


class BridgeAlreadyRunningError(RuntimeError):
    """Another Bridge process already holds the user-local instance lock."""


def default_lock_path(
    environ: Mapping[str, str] | None = None,
    *,
    platform: str = sys.platform,
) -> Path:
    source = os.environ if environ is None else environ
    return data_directory(source, platform=platform) / LOCK_FILENAME


def _lock(handle: BinaryIO) -> None:
    if sys.platform == "win32":
        handle.seek(0)
        msvcrt.locking(handle.fileno(), msvcrt.LK_NBLCK, 1)
    else:
        fcntl.flock(handle.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)


def _unlock(handle: BinaryIO) -> None:
    if sys.platform == "win32":
        handle.seek(0)
        msvcrt.locking(handle.fileno(), msvcrt.LK_UNLCK, 1)
    else:
        fcntl.flock(handle.fileno(), fcntl.LOCK_UN)


class BridgeInstanceLock:
    """Hold one byte of a stable file until the Bridge process exits."""

    def __init__(self, handle: BinaryIO) -> None:
        self._handle = handle

    @classmethod
    def acquire(cls, path: Path | None = None) -> "BridgeInstanceLock":
        lock_path = default_lock_path() if path is None else path
        lock_path.parent.mkdir(parents=True, exist_ok=True)
        handle = lock_path.open("a+b")
        try:
            handle.seek(0, os.SEEK_END)
            if handle.tell() == 0:
                handle.write(b"\0")
                handle.flush()
            handle.seek(0)
            _lock(handle)
        except OSError:
            handle.close()
            raise BridgeAlreadyRunningError(
                "another quotaframe-bridge instance is already running"
            ) from None
        return cls(handle)

    def close(self) -> None:
        handle = self._handle
        if handle.closed:
            return
        try:
            _unlock(handle)
        except OSError:
            pass
        finally:
            handle.close()

    def __enter__(self) -> "BridgeInstanceLock":
        return self

    def __exit__(self, exc_type, exc, traceback) -> None:
        self.close()
