from __future__ import annotations

import json
import tempfile
import unittest
from unittest.mock import patch
from pathlib import Path

from quotaframe_bridge.service.ownership import (
    DEVICES_FILENAME,
    OwnedDevice,
    OwnershipStore,
    default_devices_path,
    display_name,
    normalize_address,
)

M5 = "m5sticks3"
WS = "waveshare_amoled_216"
M5_LABEL = "M5StickS3"
WS_LABEL = "Waveshare AMOLED 2.16"


class AddressNormalizationTests(unittest.TestCase):
    def test_case_and_whitespace_fold_so_rescans_match(self) -> None:
        self.assertEqual(normalize_address(" aa:bb:cc:dd:ee:ff "), "AA:BB:CC:DD:EE:FF")

    def test_macos_peripheral_uuid_survives_normalization(self) -> None:
        self.assertEqual(
            normalize_address("2b7c1f0e-1111-4222-8333-444455556666"),
            "2B7C1F0E-1111-4222-8333-444455556666",
        )

    def test_unusable_addresses_return_none(self) -> None:
        for value in (None, "", "   ", 42, b"AA:BB"):
            self.assertIsNone(normalize_address(value))


class StoreTests(unittest.TestCase):
    def setUp(self) -> None:
        self._temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self._temporary.cleanup)
        self.path = Path(self._temporary.name) / "state" / DEVICES_FILENAME

    def store(self) -> OwnershipStore:
        return OwnershipStore(self.path)

    def test_missing_file_reads_as_owning_nothing(self) -> None:
        self.assertEqual(self.store().devices, ())

    def test_add_persists_and_creates_the_parent_directory(self) -> None:
        store = self.store()

        device = store.add(M5, "aa:bb:cc:dd:ee:ff", now=1700000000, name="M5StickS3")

        self.assertIsNotNone(device)
        self.assertEqual(self.store().devices, (
            OwnedDevice(target=M5, address="AA:BB:CC:DD:EE:FF", added_at=1700000000, name="M5StickS3"),
        ))

    def test_add_is_idempotent_on_the_same_address(self) -> None:
        store = self.store()
        store.add(M5, "AA:BB:CC:DD:EE:FF", name="M5StickS3")

        self.assertIsNone(store.add(M5, "aa:bb:cc:dd:ee:ff", name="M5StickS3"))
        self.assertEqual(len(store), 1)

    def test_two_boards_of_one_model_are_both_owned(self) -> None:
        store = self.store()

        store.add(M5, "AA:BB:CC:DD:EE:01", name="M5StickS3")
        store.add(M5, "AA:BB:CC:DD:EE:02", name="M5StickS3")

        self.assertEqual(len(store), 2)
        self.assertEqual({device.target for device in store}, {M5})

    def test_unknown_target_is_preserved(self) -> None:
        store = self.store()

        self.assertIsNotNone(store.add("not_a_board", "AA:BB:CC:DD:EE:FF", name="not_a_board"))
        self.assertEqual(store.devices[0].target, "not_a_board")

    def test_add_requires_an_address(self) -> None:
        with self.assertRaises(ValueError):
            self.store().add(M5, "   ", name="M5StickS3")

    def test_remove_reports_whether_anything_went_away(self) -> None:
        store = self.store()
        store.add(M5, "AA:BB:CC:DD:EE:FF", name="M5StickS3")

        self.assertTrue(store.remove("aa:bb:cc:dd:ee:ff"))
        self.assertFalse(store.remove("AA:BB:CC:DD:EE:FF"))
        self.assertEqual(self.store().devices, ())

    def test_owns_matches_case_insensitively(self) -> None:
        store = self.store()
        store.add(WS, "AA:BB:CC:DD:EE:FF", name="Waveshare AMOLED 2.16")

        self.assertTrue(store.owns("aa:bb:cc:dd:ee:ff"))
        self.assertFalse(store.owns("AA:BB:CC:DD:EE:00"))
        self.assertFalse(store.owns(None))

    def test_corrupt_file_blocks_writes_and_preserves_original(self) -> None:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.path.write_text("{not json", encoding="utf-8")

        store = self.store()
        self.assertEqual(store.devices, ())
        self.assertTrue(store.read_error)
        with self.assertRaises(RuntimeError):
            store.add(M5, "AA:BB:CC:DD:EE:FF", name="M5StickS3")
        self.assertEqual(self.path.read_text(encoding="utf-8"), "{not json")

    def test_unknown_target_survives_load(self) -> None:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.path.write_text(
            json.dumps(
                {
                    "schema_version": 1,
                    "devices": [
                        {"target": "retired_board", "address": "AA:BB:CC:DD:EE:01", "name": "Third party", "added_at": 1},
                        {"target": M5, "address": "AA:BB:CC:DD:EE:02", "name": "Panel", "added_at": 1},
                    ],
                }
            ),
            encoding="utf-8",
        )

        devices = self.store().devices

        self.assertEqual([device.target for device in devices], ["retired_board", M5])

    def test_malformed_entry_invalidates_document(self) -> None:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.path.write_text(
            json.dumps(
                {
                    "devices": [
                        "not an object",
                        {"address": "AA:BB:CC:DD:EE:01"},
                        {"target": M5},
                        {"target": M5, "address": "AA:BB:CC:DD:EE:03", "added_at": "soon"},
                    ]
                }
            ),
            encoding="utf-8",
        )

        devices = self.store().devices

        self.assertEqual(devices, ())
        store = self.store()
        self.assertEqual(store.devices, ())
        self.assertTrue(store.read_error)

    def test_write_leaves_no_temporary_file_behind(self) -> None:
        store = self.store()
        store.add(M5, "AA:BB:CC:DD:EE:FF", name="M5StickS3")

        siblings = {path.name for path in self.path.parent.iterdir()}

        self.assertEqual(siblings, {DEVICES_FILENAME})

    def test_reload_picks_up_another_writer(self) -> None:
        store = self.store()
        store.add(M5, "AA:BB:CC:DD:EE:01", name="M5StickS3")
        self.store().add(WS, "AA:BB:CC:DD:EE:02", name="Waveshare AMOLED 2.16")

        self.assertEqual(len(store), 1)
        self.assertEqual(len(store.reload()), 2)

    def test_duplicate_decoded_keys_and_addresses_preserve_bad_file(self):
        documents = [
            '{"schema_version":1,"devices":[],"\\u0064evices":[]}',
            json.dumps({"schema_version": 1, "devices": [
                {"name": "Panel", "address": address, "added_at": 1}
                for address in ("aa", "AA")]}),
        ]
        self.path.parent.mkdir(parents=True, exist_ok=True)
        for raw in documents:
            self.path.write_text(raw, encoding="utf-8")
            store = self.store()
            self.assertEqual(store.devices, ())
            self.assertTrue(store.read_error)
            with self.assertRaises(RuntimeError):
                store.add("", "BB", name="New panel")
            self.assertEqual(self.path.read_text(encoding="utf-8"), raw)

    def test_failed_replace_does_not_publish_new_snapshot(self):
        store = self.store()
        saved = store.add("", "AA", name="Panel")
        before = self.path.read_bytes()
        with patch("quotaframe_bridge.service.ownership.os.replace", side_effect=OSError):
            with self.assertRaises(OSError):
                store.add("", "BB", name="Other")
        self.assertEqual(store.devices, (saved,))
        self.assertEqual(self.path.read_bytes(), before)


class DisplayNameTests(unittest.TestCase):
    def test_one_board_per_model_keeps_the_plain_label(self) -> None:
        owned = (
            OwnedDevice(M5, "AA:BB:CC:DD:EE:01", name="M5StickS3"),
            OwnedDevice(WS, "AA:BB:CC:DD:EE:02", name="Waveshare AMOLED 2.16"),
        )

        self.assertEqual(display_name(owned[0], owned), M5_LABEL)
        self.assertEqual(display_name(owned[1], owned), WS_LABEL)

    def test_two_boards_of_one_model_are_disambiguated_by_address(self) -> None:
        owned = (
            OwnedDevice(M5, "AA:BB:CC:DD:EE:01", name="M5StickS3"),
            OwnedDevice(M5, "AA:BB:CC:DD:EE:02", name="M5StickS3"),
        )

        self.assertEqual(display_name(owned[0], owned), f"{M5_LABEL} ·EE01")
        self.assertEqual(display_name(owned[1], owned), f"{M5_LABEL} ·EE02")

    def test_macos_uuid_addresses_disambiguate_too(self) -> None:
        owned = (
            OwnedDevice(WS, "2B7C1F0E-1111-4222-8333-4444555566A1", name="Waveshare AMOLED 2.16"),
            OwnedDevice(WS, "2B7C1F0E-1111-4222-8333-4444555566B2", name="Waveshare AMOLED 2.16"),
        )

        self.assertEqual(display_name(owned[0], owned), f"{WS_LABEL} ·66A1")
        self.assertEqual(display_name(owned[1], owned), f"{WS_LABEL} ·66B2")

    def test_matching_four_character_suffixes_expand_until_unique(self) -> None:
        owned = (
            OwnedDevice(M5, "AA:BB:CC:01:EE:FF", name="M5StickS3"),
            OwnedDevice(M5, "AA:BB:CC:02:EE:FF", name="M5StickS3"),
        )

        self.assertEqual(display_name(owned[0], owned), f"{M5_LABEL} ·1EEFF")
        self.assertEqual(display_name(owned[1], owned), f"{M5_LABEL} ·2EEFF")


class DefaultPathTests(unittest.TestCase):
    def test_windows_path_sits_beside_the_instance_lock(self) -> None:
        path = default_devices_path(
            {"LOCALAPPDATA": "C:\\Users\\x\\AppData\\Local"},
            platform="win32",
        )

        self.assertEqual(path.name, DEVICES_FILENAME)
        self.assertEqual(path.parent.name, "quotaframe")

    def test_macos_path_uses_application_support(self) -> None:
        path = default_devices_path({"HOME": "/Users/x"}, platform="darwin")

        self.assertEqual(
            path,
            Path("/Users/x/Library/Application Support/quotaframe")
            / DEVICES_FILENAME,
        )


if __name__ == "__main__":
    unittest.main()
