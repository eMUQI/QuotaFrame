from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from quotaframe_bridge.domain.models import Provider, SourceState
from quotaframe_bridge.service.state import UsageStateStore
from quotaframe_bridge.sources.mock import MockUsageSource


REPOSITORY = Path(__file__).parents[2]


class MockSourceTests(unittest.IsolatedAsyncioTestCase):
    async def test_mock_is_deterministic_and_changes_each_cycle(self) -> None:
        source = MockUsageSource()

        first = await source.collect(attempted_at=1_000)
        second = await source.collect(attempted_at=1_060)

        self.assertEqual(first[Provider.CODEX].short.used_percent, 12)
        self.assertEqual(first[Provider.CLAUDE].week.used_percent, 48)
        self.assertNotEqual(
            first[Provider.CODEX].short.used_percent,
            second[Provider.CODEX].short.used_percent,
        )
        self.assertEqual(first[Provider.CODEX].short.reset_at, 1_000 + 2 * 60 * 60)


class StateStoreTests(unittest.IsolatedAsyncioTestCase):
    async def test_source_failure_retains_sample_without_expiry_and_recovers(self) -> None:
        source = MockUsageSource()
        valid = await source.collect(attempted_at=1_000)
        store = UsageStateStore()
        store.merge_collection(valid, attempted_at=1_000)

        for attempted_at in (1_060, 1_181, 100_000):
            retained = store.mark_all_unavailable(attempted_at=attempted_at)
            self.assertEqual(dict(retained), valid)
            self.assertEqual(store.last_collection_attempt, attempted_at)

        recovered = await source.collect(attempted_at=1_200)
        self.assertEqual(dict(store.merge_collection(recovered, attempted_at=1_200)), recovered)
        self.assertEqual(dict(store.mark_all_unavailable(attempted_at=1_260)), recovered)

    async def test_provider_failure_does_not_block_other_provider_update(self) -> None:
        source = MockUsageSource()
        valid = await source.collect(attempted_at=1_000)
        store = UsageStateStore()
        store.merge_collection(valid, attempted_at=1_000)
        fresh = await source.collect(attempted_at=1_060)
        merged = store.merge_collection(
            {Provider.CODEX: fresh[Provider.CODEX]}, attempted_at=1_060
        )
        self.assertEqual(merged[Provider.CODEX], fresh[Provider.CODEX])
        self.assertEqual(merged[Provider.CLAUDE], valid[Provider.CLAUDE])
        expired = store.merge_collection(
            {Provider.CODEX: fresh[Provider.CODEX]}, attempted_at=1_181
        )
        self.assertEqual(expired[Provider.CLAUDE], valid[Provider.CLAUDE])
        self.assertEqual(expired[Provider.CODEX], fresh[Provider.CODEX])

    async def test_initial_failure_has_no_data_but_old_valid_samples_are_retained(self) -> None:
        store = UsageStateStore()
        initial = store.mark_all_unavailable(attempted_at=1_000)
        self.assertTrue(all(usage.state is SourceState.UNAVAILABLE for usage in initial.values()))
        valid = await MockUsageSource().collect(attempted_at=1_000)
        store.merge_collection(valid, attempted_at=1_500)
        expired = store.mark_all_unavailable(attempted_at=1_501)
        self.assertEqual(dict(expired), valid)


class DryRunCliTests(unittest.TestCase):
    def test_mock_dry_run_outputs_only_two_sanitized_usage_lines(self) -> None:
        env = dict(os.environ)
        env["PYTHONPATH"] = str(REPOSITORY / "bridge" / "src")
        # The instance lock lives under the platform's user data directory, so
        # without its own the run would fail whenever the Bridge is already
        # running. LOCALAPPDATA roots that directory on Windows, HOME on macOS.
        lock_home = tempfile.mkdtemp(prefix="quotaframe-bridge-test-")
        self.addCleanup(shutil.rmtree, lock_home, ignore_errors=True)
        env["LOCALAPPDATA"] = lock_home
        env["HOME"] = lock_home
        completed = subprocess.run(
            [
                sys.executable,
                "-m",
                "quotaframe_bridge",
                "--mock",
                "--dry-run",
                "--cycles",
                "1",
                "--interval",
                "0",
            ],
            cwd=REPOSITORY,
            env=env,
            text=True,
            capture_output=True,
            check=True,
        )

        messages = [json.loads(line) for line in completed.stdout.splitlines()]
        self.assertEqual(
            [item["provider"] for item in messages],
            ["codex", "claude"],
        )
        self.assertTrue(all(item["cmd"] == "usage" for item in messages))
        forbidden = {
            "email",
            "token",
            "cookie",
            "key",
            "account",
            "cost",
            "credit",
            "plan",
        }
        serialized_keys = {key.lower() for item in messages for key in item}
        self.assertFalse(forbidden.intersection(serialized_keys))

    def test_dry_run_requires_mock_to_avoid_printing_real_usage(self) -> None:
        env = dict(os.environ)
        env["PYTHONPATH"] = str(REPOSITORY / "bridge" / "src")
        completed = subprocess.run(
            [sys.executable, "-m", "quotaframe_bridge", "--dry-run"],
            cwd=REPOSITORY,
            env=env,
            text=True,
            capture_output=True,
        )

        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("--dry-run requires --mock", completed.stderr)


if __name__ == "__main__":
    unittest.main()
