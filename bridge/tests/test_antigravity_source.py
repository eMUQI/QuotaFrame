import json
import unittest

from quotaframe_bridge.domain.models import Provider, SourceState
from quotaframe_bridge.sources.antigravity import parse_gemini_report


class AntigravityReportTests(unittest.TestCase):
    def test_weekly_only_report_ignores_third_party_and_keeps_zero_usage(self) -> None:
        report = {
            "status": "SUCCESS",
            "num_turns": 0,
            "command": {"name": "usage", "data": {"groups": [
                {"buckets": [{"id": "gemini-weekly", "remaining_fraction": 1,
                              "reset_time": "2026-11-20T00:00:00Z"}]},
                {"buckets": [{"id": "3p-5h", "remaining_fraction": 0}]},
            ]}},
        }
        usage = parse_gemini_report(json.dumps(report), attempted_at=100)
        self.assertEqual(usage.provider, Provider.GEMINI)
        self.assertEqual(usage.state, SourceState.PARTIAL)
        self.assertIsNone(usage.short)
        self.assertEqual(usage.week.used_percent, 0)
        self.assertEqual(usage.week.reset_at, 1795132800)

    def test_model_response_is_rejected(self) -> None:
        with self.assertRaises(ValueError):
            parse_gemini_report('{"status":"SUCCESS","num_turns":1}', attempted_at=100)
