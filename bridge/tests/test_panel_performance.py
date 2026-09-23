import unittest

from tools.summarize_panel_performance import summarize


class PanelPerformanceEvidenceTests(unittest.TestCase):
    def test_dma_pairing_missing_samples_and_runtime_wrap(self):
        log = """PERF_DISPLAY id=1 kind=render start_us=100 first_flush_us=200 end_us=1100 bytes=19200 strips=1
PERF_DISPLAY id=1 kind=dma start_us=100 first_flush_us=200 end_us=1500 bytes=19200 strips=1
PERF_DISPLAY id=2 kind=render start_us=2000 first_flush_us=2100 end_us=3000 bytes=9600 strips=1
PERF_TASK_SNAPSHOT t_us=1 total_us=4294967040 count=1 capacity=48 snapshot_us=100
PERF_TASK t_us=1 id=3 name=worker core=1 runtime_us=4294967040 stack_bytes=3000 priority=2
PERF_TASK_SNAPSHOT t_us=2 total_us=744 count=1 capacity=48 snapshot_us=100
PERF_TASK t_us=2 id=3 name=worker core=1 runtime_us=244 stack_bytes=2900 priority=2
PERF_TASK t_us=3 id=3
"""
        result = summarize(log)
        timing = result["display"]["page=unlabelled,forced=unknown"]
        self.assertEqual(timing["dma_complete_ms"]["median"], 1.4)
        self.assertEqual(timing["dma_minus_render_ms"]["median"], .4)
        self.assertEqual(result["unmatched_frames"], 1)
        self.assertEqual(result["malformed_records"], 1)
        self.assertEqual(result["tasks"][0]["single_core_percent"], 50)
        self.assertEqual(result["tasks"][0]["system_capacity_percent"], 25)
        self.assertEqual(result["tasks"][0]["stack_min_bytes"], 2900)


if __name__ == "__main__":
    unittest.main()
