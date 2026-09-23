from __future__ import annotations

import base64
import json
import unittest

from quotaframe_bridge.protocol.ota_messages import (
    FOLDER_PUSH_FIRMWARE_PATH,
    FOLDER_PUSH_MANIFEST_PATH,
    OtaMessageError,
    decode_folder_push_chunk,
    encode_folder_push_begin,
    encode_folder_push_chunk,
    encode_folder_push_end,
    encode_folder_push_file,
    encode_folder_push_file_end,
    encode_ota_abort,
    encode_ota_manifest,
    parse_decimal_text,
    parse_ota_status,
)


class OtaMessageTests(unittest.TestCase):
    def test_manifest_has_exact_canonical_shape_without_line_delimiter(self) -> None:
        digest = "ab" * 32
        encoded = encode_ota_manifest("m5sticks3", 1234, digest, "0.5.0")
        payload = json.loads(encoded)

        self.assertEqual(
            payload,
            {
                "schema": "1",
                "firmware_project": "quotaframe",
                "target": "m5sticks3",
                "sha256": digest,
                "size": 1234,
                "version": "0.5.0",
            },
        )
        self.assertFalse(encoded.endswith(b"\n"))

    def test_folder_push_envelope_has_exact_wire_shape(self) -> None:
        self.assertEqual(
            json.loads(encode_folder_push_begin(4321)),
            {"cmd": "char_begin", "name": "firmware", "total": 4321},
        )
        self.assertEqual(
            json.loads(encode_folder_push_file(FOLDER_PUSH_MANIFEST_PATH, 123)),
            {"cmd": "file", "path": "manifest.json", "size": 123},
        )
        self.assertEqual(
            json.loads(encode_folder_push_file(FOLDER_PUSH_FIRMWARE_PATH, 4198)),
            {"cmd": "file", "path": "firmware.bin", "size": 4198},
        )
        self.assertEqual(
            json.loads(encode_folder_push_file_end()), {"cmd": "file_end"}
        )
        self.assertEqual(json.loads(encode_folder_push_end()), {"cmd": "char_end"})

    def test_chunk_round_trips_a_full_2880_byte_block(self) -> None:
        block = bytes(index % 251 for index in range(2880))
        line = encode_folder_push_chunk(block)
        payload = json.loads(line)

        self.assertEqual(set(payload), {"cmd", "d"})
        self.assertEqual(len(payload["d"]), 3840)
        self.assertEqual(base64.b64decode(payload["d"], validate=True), block)
        self.assertEqual(decode_folder_push_chunk(payload), block)

    def test_abort_retains_the_allowlisted_recovery_command(self) -> None:
        self.assertEqual(
            json.loads(encode_ota_abort(10)),
            {"cmd": "ota_abort", "seq": "10", "v": "1"},
        )

    def test_rejects_invalid_manifest_chunk_path_and_decimal(self) -> None:
        with self.assertRaises(OtaMessageError):
            encode_ota_manifest("m5sticks3", 4, "AB" * 32, "0.5.0")
        with self.assertRaises(OtaMessageError):
            encode_folder_push_chunk(b"x" * 2881)
        with self.assertRaises(OtaMessageError):
            encode_folder_push_file("other.bin", 4)
        for value in ("01", "+1", "-1", "", 1, True):
            with self.subTest(value=value), self.assertRaises(OtaMessageError):
                parse_decimal_text(value, "off")

    def test_status_parses_low_power_error(self) -> None:
        parsed = parse_ota_status(
            {"phase": "idle", "err": "low_power", "off": "0", "size": "0"}
        )

        self.assertEqual(parsed.phase, "idle")
        self.assertEqual(parsed.error, "low_power")

    def test_status_rejects_unknown_error_word(self) -> None:
        with self.assertRaises(OtaMessageError):
            parse_ota_status(
                {"phase": "idle", "err": "battery", "off": "0", "size": "0"}
            )


if __name__ == "__main__":
    unittest.main()
