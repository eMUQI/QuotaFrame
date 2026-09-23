from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from quotaframe_bridge.cli.instance_lock import (
    BridgeAlreadyRunningError,
    BridgeInstanceLock,
)


class BridgeInstanceLockTests(unittest.TestCase):
    def test_second_lock_is_rejected_until_first_handle_closes(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            lock_path = Path(temporary) / "bridge.lock"
            first = BridgeInstanceLock.acquire(lock_path)
            try:
                with self.assertRaises(BridgeAlreadyRunningError):
                    BridgeInstanceLock.acquire(lock_path)
            finally:
                first.close()

            second = BridgeInstanceLock.acquire(lock_path)
            second.close()

    def test_missing_parent_directory_is_created(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            lock_path = Path(temporary) / "nested" / "bridge.lock"

            with BridgeInstanceLock.acquire(lock_path):
                self.assertTrue(lock_path.exists())

    def test_context_manager_releases_lock(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            lock_path = Path(temporary) / "bridge.lock"
            with BridgeInstanceLock.acquire(lock_path):
                with self.assertRaises(BridgeAlreadyRunningError):
                    BridgeInstanceLock.acquire(lock_path)

            with BridgeInstanceLock.acquire(lock_path):
                pass


if __name__ == "__main__":
    unittest.main()
