from __future__ import annotations

import base64
import json
import unittest

from tools.measure_ota_ble import (
    LineInbox, ProbeResult, build_probe_line, summarize_samples, validate_probe_reply,
)


class ProbePayloadTests(unittest.TestCase):
    def test_probe_line_is_valid_json_and_exactly_one_line(self) -> None:
        line = build_probe_line(seq=7, payload_bytes=2880)

        parsed = json.loads(line)
        self.assertTrue(line.endswith(b"\n"))
        self.assertNotIn(b"\n", line[:-1])
        self.assertEqual(parsed["cmd"], "ota_probe_00000007")
        self.assertEqual(parsed["seq"], "7")
        self.assertEqual(
            len(base64.b64decode(parsed["b64"], validate=True)),
            2880,
        )

    def test_probe_result_reports_payload_throughput(self) -> None:
        result = ProbeResult(payload_bytes=2880, elapsed_seconds=2.0)

        self.assertEqual(result.kib_per_second, 1.40625)


class ProbeEvidenceTests(unittest.IsolatedAsyncioTestCase):
    async def test_stale_ack_cannot_complete_next_probe(self) -> None:
        inbox = LineInbox()
        inbox.feed(b'{"ack":"ota_probe_00000006","ok":false,"n":0,"error":"unknown_command"}\n')
        expected = {"ack": "ota_probe_00000007", "ok": False, "n": 0, "error": "unknown_command"}
        encoded = (json.dumps(expected) + "\n").encode()
        inbox.feed(encoded[:20])
        inbox.feed(encoded[20:])
        reply = await inbox.receive_reply(expected["ack"], .1)
        validate_probe_reply(reply, expected["ack"])
        with self.assertRaises(RuntimeError):
            validate_probe_reply({**reply, "ok": True}, expected["ack"])
        with self.assertRaises(TimeoutError):
            await inbox.receive_reply("ota_probe_00000008", .01)

    def test_failed_attempt_time_reduces_goodput_and_warmup_is_excluded(self) -> None:
        samples = [
            {"warmup": True, "success": True, "payload_bytes": 1024, "elapsed_s": 10},
            {"warmup": False, "success": True, "payload_bytes": 2048, "elapsed_s": 1},
            {"warmup": False, "success": False, "payload_bytes": 2048, "elapsed_s": 3},
        ]
        summary = summarize_samples(samples)
        self.assertEqual((summary["attempted"], summary["succeeded"], summary["failed"]), (2, 1, 1))
        self.assertEqual(summary["goodput_kib_s"], .5)
        self.assertEqual(summary["success_rtt_p95_s"], 1)


if __name__ == "__main__":
    unittest.main()
