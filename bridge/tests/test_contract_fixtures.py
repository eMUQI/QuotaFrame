from __future__ import annotations

import json
import unittest
from pathlib import Path

from quotaframe_bridge.protocol.lines import LineDecoder

ROOT = Path(__file__).resolve().parents[2]
EXAMPLES = ROOT / "protocol" / "examples"
ALLOWED_KEYS = {
    "cmd",
    "v",
    "seq",
    "provider",
    "state",
    "sampled_at",
    "sent_at",
    "short_used_pct",
    "short_reset_at",
    "week_used_pct",
    "week_reset_at",
}


class ContractFixtureTests(unittest.TestCase):
    def test_valid_usage_fixtures_decode_and_use_only_allowlisted_keys(self) -> None:
        paths = (
            EXAMPLES / "usage-ok.jsonl",
            EXAMPLES / "usage-partial.jsonl",
            EXAMPLES / "usage-unavailable.jsonl",
        )
        for path in paths:
            with self.subTest(path=path.name):
                messages = LineDecoder(1024).feed(path.read_bytes())
                self.assertEqual(len(messages), 1)
                payload = messages[0]
                self.assertEqual(payload["cmd"], "usage")
                self.assertEqual(payload["v"], "1")
                self.assertLessEqual(set(payload), ALLOWED_KEYS)
                for key, value in payload.items():
                    if key not in {"cmd", "provider", "state"}:
                        with self.subTest(path=path.name, key=key):
                            self.assertIsInstance(value, str)
                            self.assertTrue(value.isdigit())

    def test_invalid_fixtures_cover_range_type_and_orphan_reset(self) -> None:
        percent = json.loads((EXAMPLES / "usage-invalid.jsonl").read_text())
        numeric = json.loads(
            (EXAMPLES / "usage-invalid-numeric.jsonl").read_text()
        )
        orphan = json.loads(
            (EXAMPLES / "usage-invalid-orphan-reset.jsonl").read_text()
        )

        self.assertGreater(int(percent["short_used_pct"]), 100)
        self.assertIsInstance(numeric["short_used_pct"], int)
        self.assertIn("short_reset_at", orphan)
        self.assertNotIn("short_used_pct", orphan)


if __name__ == "__main__":
    unittest.main()
