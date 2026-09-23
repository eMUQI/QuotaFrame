from __future__ import annotations

import asyncio
import tempfile
import unittest
from unittest.mock import patch
from pathlib import Path

from quotaframe_bridge import ui
from quotaframe_bridge.service.adoption import Discovery
from quotaframe_bridge.service.ownership import OwnershipStore
from quotaframe_bridge.protocol.messages import ProtocolError, parse_status
from quotaframe_bridge.ui.status import DeviceStatus, TrayState, format_info_lines
from quotaframe_bridge.service.devices import (
    TrayServiceGraph,
    build_tray_graph,
    run_adoption,
)

from quotaframe_bridge.ui.tray_service import snapshot_tray_status

M5 = "m5sticks3"
WS = "waveshare_amoled_216"
M5_LABEL = "M5StickS3"
WS_LABEL = "Waveshare AMOLED 2.16"


class StoreFixture(unittest.TestCase):
    def setUp(self) -> None:
        self._temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self._temporary.cleanup)
        self.path = Path(self._temporary.name) / "devices.json"

    def graph(self, owned: tuple[tuple[str, str], ...] = ()) -> TrayServiceGraph:
        store = OwnershipStore(self.path)
        for target, address in owned:
            store.add(target, address, name={M5: M5_LABEL, WS: WS_LABEL}[target])
        return build_tray_graph(
            usage_source_factory=object,
            pairer_factory=object,
            store=store,
        )


class GraphAssemblyTests(StoreFixture):
    def test_status_metadata_failure_keeps_live_name_and_saved_snapshot(self):
        graph = self.graph(((M5, "AA"),))
        device = graph.get("AA")
        session = device.session
        before = self.path.read_bytes()
        status = parse_status({"ack": "status", "ok": True, "n": 0, "data": {
            "name": "New name", "protocol": 1, "sec": True, "caps": ["usage.v1"]}})
        with patch("quotaframe_bridge.service.ownership.os.replace", side_effect=OSError):
            graph._metadata("AA", device.manager, status)
        self.assertTrue(device.metadata_error)
        self.assertEqual(device.name, "New name")
        self.assertIs(device.session, session)
        self.assertEqual(self.path.read_bytes(), before)
        graph._metadata("AA", device.manager, status)
        self.assertFalse(device.metadata_error)
        self.assertEqual(OwnershipStore(self.path).devices[0].name, "New name")

    def test_owning_nothing_builds_no_sessions_and_no_scanning(self) -> None:
        graph = self.graph()

        self.assertEqual(graph.devices, ())
        self.assertEqual(graph.service.sessions, ())

    def test_one_session_per_owned_address(self) -> None:
        graph = self.graph(((M5, "AA:BB:CC:DD:EE:01"), (WS, "AA:BB:CC:DD:EE:02")))

        self.assertEqual(graph.names, (M5_LABEL, WS_LABEL))
        self.assertEqual(len(graph.service.sessions), 2)

    def test_two_boards_of_one_model_each_get_a_session(self) -> None:
        graph = self.graph(((M5, "AA:BB:CC:DD:EE:01"), (M5, "AA:BB:CC:DD:EE:02")))

        self.assertEqual(graph.names, (f"{M5_LABEL} ·EE01", f"{M5_LABEL} ·EE02"))
        self.assertEqual(len(graph.service.sessions), 2)

    def test_a_board_the_user_does_not_own_produces_nothing(self) -> None:
        """An unowned board creates no session, so it cannot trigger an endless scan."""

        graph = self.graph(((M5, "AA:BB:CC:DD:EE:01"),))

        self.assertEqual(graph.names, (M5_LABEL,))
        self.assertNotIn(WS_LABEL, graph.names)

    def test_session_transport_is_built_for_the_owned_address_and_target(self) -> None:
        graph = self.graph(((WS, "AA:BB:CC:DD:EE:02"),))
        device = graph.devices[0]

        transport = device.manager._factory()

        self.assertEqual(transport.address, "AA:BB:CC:DD:EE:02")
        self.assertFalse(hasattr(transport, "expected_target"))
        self.assertIsNone(transport.device_name)

    def test_adoption_names_and_sessions_follow_the_owned_set(self) -> None:
        graph = self.graph(((M5, "AA:BB:CC:DD:EE:01"),))

        self.assertEqual(graph.names, (M5_LABEL,))

        graph.adopt(Discovery(target=M5, address="AA:BB:CC:DD:EE:02", name=M5_LABEL))

        self.assertEqual(graph.names, (f"{M5_LABEL} ·EE01", f"{M5_LABEL} ·EE02"))
        self.assertEqual(
            tuple(session.label for session in graph.service.sessions),
            (f"{M5_LABEL} ·EE01", f"{M5_LABEL} ·EE02"),
        )

    def test_adopting_unknown_target_creates_session(self) -> None:
        graph = self.graph()

        self.assertIsNotNone(graph.adopt(Discovery(target="retired_board", address="AA:BB:CC:DD:EE:01", name="Third party")))
        self.assertEqual(len(graph.devices), 1)

    def test_get_finds_the_device_by_address(self) -> None:
        graph = self.graph(((M5, "AA:BB:CC:DD:EE:01"), (M5, "AA:BB:CC:DD:EE:02")))

        found = graph.get("AA:BB:CC:DD:EE:02")

        self.assertIsNotNone(found)
        assert found is not None
        self.assertEqual(found.address, "AA:BB:CC:DD:EE:02")
        self.assertIsNone(graph.get("AA:BB:CC:DD:EE:03"))


class ForgetTests(StoreFixture):
    def test_forget_drops_the_session_the_name_and_the_file_entry(self) -> None:
        graph = self.graph(((M5, "AA:BB:CC:DD:EE:01"), (WS, "AA:BB:CC:DD:EE:02")))

        removed = asyncio.run(graph.forget("aa:bb:cc:dd:ee:01", expected=graph.get("aa:bb:cc:dd:ee:01")))

        self.assertTrue(removed)
        self.assertEqual(graph.names, (WS_LABEL,))
        self.assertEqual(len(graph.service.sessions), 1)
        self.assertEqual(
            [device.address for device in OwnershipStore(self.path)],
            ["AA:BB:CC:DD:EE:02"],
        )

    def test_forgetting_the_second_board_restores_the_plain_name(self) -> None:
        graph = self.graph(((M5, "AA:BB:CC:DD:EE:01"), (M5, "AA:BB:CC:DD:EE:02")))

        asyncio.run(graph.forget("AA:BB:CC:DD:EE:02", expected=graph.get("AA:BB:CC:DD:EE:02")))

        self.assertEqual(graph.names, (M5_LABEL,))
        self.assertEqual(graph.service.sessions[0].label, M5_LABEL)

    def test_forgetting_an_unknown_address_is_a_no_op(self) -> None:
        graph = self.graph(((M5, "AA:BB:CC:DD:EE:01"),))

        self.assertFalse(asyncio.run(graph.forget("AA:BB:CC:DD:EE:99", expected=graph.get("AA:BB:CC:DD:EE:99"))))
        self.assertEqual(len(graph.devices), 1)

    def test_persistence_failure_leaves_the_live_session_attached(self) -> None:
        graph = self.graph(((M5, "AA:BB:CC:DD:EE:01"),))

        def fail_remove(_address: str) -> bool:
            raise OSError("disk full")

        graph.store.remove = fail_remove  # type: ignore[method-assign]

        with self.assertRaisesRegex(OSError, "disk full"):
            asyncio.run(graph.forget("AA:BB:CC:DD:EE:01", expected=graph.get("AA:BB:CC:DD:EE:01")))

        self.assertEqual(graph.names, (M5_LABEL,))
        self.assertEqual(len(graph.service.sessions), 1)
        self.assertIsNotNone(graph.get("AA:BB:CC:DD:EE:01"))


class ExclusionTests(StoreFixture):
    def test_owned_and_declined_addresses_are_both_skipped_by_scans(self) -> None:
        graph = self.graph(((M5, "AA:BB:CC:DD:EE:01"),))

        graph.decline("aa:bb:cc:dd:ee:09")

        self.assertEqual(
            graph.excluded_addresses(),
            frozenset({"AA:BB:CC:DD:EE:01", "AA:BB:CC:DD:EE:09"}),
        )


class FakeStatus:
    def __init__(self, target: str) -> None:
        self.target = target
        self.name = {M5: M5_LABEL, WS: WS_LABEL}.get(target, "Third party")
        self.firmware_project = ""


class FakeTransport:
    """Stands in for the adoption-scan transport."""

    instances: list["FakeTransport"] = []

    def __init__(self, **kwargs: object) -> None:
        self.kwargs = kwargs
        self.status: FakeStatus | None = None
        self.found_address: str | None = None
        self.closed = False
        FakeTransport.instances.append(self)

    async def connect(self) -> None:
        raise NotImplementedError

    async def close(self) -> None:
        self.closed = True


class DiscoveryTests(StoreFixture):
    def test_incompatible_discovery_is_visible_and_not_adopted(self) -> None:
        class Incompatible(FakeTransport):
            async def connect(self) -> None:
                self.found_address = "AA:BB:CC:DD:EE:08"
                parse_status({"ack": "status", "n": 0, "ok": True, "data": {
                    "name": "Panel", "sec": True, "protocol": 2,
                    "page": "overview", "caps": ['usage.v1'],
                }})

        self.patch_transport(Incompatible)
        graph = self.graph()
        asyncio.run(run_adoption(graph, empty_scan_limit=1))
        status = snapshot_tray_status(graph)
        self.assertEqual(graph.devices, ())
        self.assertIn("AA:BB:CC:DD:EE:08", graph.excluded_addresses())
        self.assertTrue(FakeTransport.instances[0].closed)
        self.assertIs(status.state, TrayState.WARN)
        self.assertEqual(format_info_lines(status)[0], "新设备需更新 Bridge/固件")

    def setUp(self) -> None:
        super().setUp()
        FakeTransport.instances = []

    def patch_transport(self, factory) -> None:
        import quotaframe_bridge.service.adoption as module

        original = module.BleakNusTransport
        module.BleakNusTransport = factory
        self.addCleanup(lambda: setattr(module, "BleakNusTransport", original))

    def test_a_new_board_reports_its_target_and_address(self) -> None:
        class Found(FakeTransport):
            async def connect(self) -> None:
                self.status = FakeStatus(M5)
                self.found_address = "AA:BB:CC:DD:EE:07"
                if self.found_address in self.kwargs["exclude_addresses"]:
                    raise _nus_error("usage panel was not found")

        self.patch_transport(Found)
        graph = self.graph()

        result = asyncio.run(run_adoption(graph, retry_delay=0, empty_scan_limit=1))

        self.assertEqual(len(result), 1)
        self.assertEqual(result[0].owned.target, M5)
        self.assertEqual(result[0].address, "AA:BB:CC:DD:EE:07")
        self.assertEqual(result[0].name, M5_LABEL)
        self.assertTrue(FakeTransport.instances[0].closed)

    def test_owned_addresses_are_handed_to_the_scan_as_exclusions(self) -> None:
        class Nothing(FakeTransport):
            async def connect(self) -> None:
                raise _nus_error("usage panel was not found")

        self.patch_transport(Nothing)
        graph = self.graph(((M5, "AA:BB:CC:DD:EE:01"),))

        asyncio.run(run_adoption(graph, retry_delay=0, empty_scan_limit=1))

        self.assertEqual(
            FakeTransport.instances[0].kwargs["exclude_addresses"],
            frozenset({"AA:BB:CC:DD:EE:01"}),
        )

    def test_a_declined_pairing_is_not_offered_again_this_run(self) -> None:
        class Declined(FakeTransport):
            async def connect(self) -> None:
                self.found_address = "AA:BB:CC:DD:EE:09"
                raise _pairing_error()

        self.patch_transport(Declined)
        graph = self.graph()

        result = asyncio.run(run_adoption(graph, retry_delay=0, empty_scan_limit=1))

        self.assertEqual(result, ())
        self.assertIn("AA:BB:CC:DD:EE:09", graph.excluded_addresses())

    def test_an_invalid_status_is_not_offered_again_this_run(self) -> None:
        class Invalid(FakeTransport):
            async def connect(self) -> None:
                self.found_address = "AA:BB:CC:DD:EE:08"
                raise ProtocolError("unsupported status")

        self.patch_transport(Invalid)
        graph = self.graph()

        result = asyncio.run(run_adoption(graph, retry_delay=0, empty_scan_limit=1))

        self.assertEqual(result, ())
        self.assertIn("AA:BB:CC:DD:EE:08", graph.excluded_addresses())

    def test_an_unexpected_ble_failure_does_not_abort_the_adoption_run(self) -> None:
        class Failed(FakeTransport):
            async def connect(self) -> None:
                self.found_address = "AA:BB:CC:DD:EE:06"
                raise RuntimeError("backend failure")

        self.patch_transport(Failed)
        graph = self.graph()

        result = asyncio.run(run_adoption(graph, retry_delay=0, empty_scan_limit=1))

        self.assertEqual(result, ())
        self.assertIn("AA:BB:CC:DD:EE:06", graph.excluded_addresses())

    def test_usage_only_board_can_be_adopted(self) -> None:
        class Anonymous(FakeTransport):
            async def connect(self) -> None:
                self.status = FakeStatus("")
                self.found_address = "AA:BB:CC:DD:EE:07"
                if self.found_address in self.kwargs["exclude_addresses"]:
                    raise _nus_error("usage panel was not found")

        self.patch_transport(Anonymous)
        graph = self.graph()

        result = asyncio.run(run_adoption(graph, retry_delay=0, empty_scan_limit=1))
        self.assertEqual(len(result), 1)
        self.assertIs(graph.get(result[0].address), result[0])

    def test_adoption_stops_after_consecutive_empty_scans(self) -> None:
        scans = 0

        class Nothing(FakeTransport):
            async def connect(self) -> None:
                nonlocal scans
                scans += 1
                raise _nus_error("usage panel was not found")

        self.patch_transport(Nothing)
        graph = self.graph()

        adopted = asyncio.run(
            run_adoption(graph, retry_delay=0, empty_scan_limit=3)
        )

        self.assertEqual(adopted, ())
        self.assertEqual(scans, 3)

    def test_adoption_takes_every_board_answering_then_stops(self) -> None:
        answers = [(M5, "AA:BB:CC:DD:EE:01"), (WS, "AA:BB:CC:DD:EE:02")]
        announced: list[str] = []

        class Sequence(FakeTransport):
            async def connect(self) -> None:
                if not answers:
                    raise _nus_error("usage panel was not found")
                target, address = answers.pop(0)
                self.status = FakeStatus(target)
                self.found_address = address

        self.patch_transport(Sequence)
        graph = self.graph()

        adopted = asyncio.run(
            run_adoption(
                graph,
                on_adopted=lambda device: announced.append(device.name),
                retry_delay=0,
                empty_scan_limit=2,
            )
        )

        self.assertEqual(len(adopted), 2)
        self.assertEqual(announced, [M5_LABEL, WS_LABEL])
        self.assertEqual(graph.names, (M5_LABEL, WS_LABEL))
        self.assertEqual(
            [device.address for device in OwnershipStore(self.path)],
            ["AA:BB:CC:DD:EE:01", "AA:BB:CC:DD:EE:02"],
        )

    def test_unknown_target_is_adopted_then_excluded(self) -> None:
        scans = 0

        class Unsupported(FakeTransport):
            async def connect(self) -> None:
                nonlocal scans
                scans += 1
                self.status = FakeStatus("retired_board")
                self.found_address = "AA:BB:CC:DD:EE:09"

        self.patch_transport(Unsupported)
        graph = self.graph()

        adopted = asyncio.run(
            run_adoption(graph, retry_delay=0, empty_scan_limit=3)
        )

        self.assertEqual(len(adopted), 1)
        self.assertEqual(scans, 4)
        self.assertIn("AA:BB:CC:DD:EE:09", graph.excluded_addresses())


def _nus_error(message: str) -> Exception:
    from quotaframe_bridge.transports.bleak_nus import NusTransportError

    return NusTransportError(message)


def _pairing_error() -> Exception:
    from quotaframe_bridge.pairing.base import PairingError

    return PairingError("declined")


class SnapshotTests(StoreFixture):
    def test_owned_incompatible_device_has_actionable_status(self) -> None:
        graph = self.graph(((M5, "AA:BB:CC:DD:EE:01"),))
        graph.devices[0].manager.incompatible = True
        status = snapshot_tray_status(graph)
        self.assertIs(status.state, TrayState.WARN)
        self.assertIn("需更新 Bridge/固件", format_info_lines(status)[0])

    def test_snapshot_reads_shared_service_state(self) -> None:
        graph = self.graph(((M5, "AA:BB:CC:DD:EE:01"), (WS, "AA:BB:CC:DD:EE:02")))
        graph.service.consecutive_failures = 2
        graph.service.resolution_error = "not_found"

        status = snapshot_tray_status(graph)

        self.assertEqual(
            status.devices,
            (
                DeviceStatus(M5_LABEL, connected=False, ever_connected=False, address="AA:BB:CC:DD:EE:01", session=graph.devices[0].session),
                DeviceStatus(WS_LABEL, connected=False, ever_connected=False, address="AA:BB:CC:DD:EE:02", session=graph.devices[1].session),
            ),
        )
        self.assertEqual(status.providers, {})
        self.assertEqual(status.consecutive_failures, 2)
        self.assertEqual(status.resolution_error, "not_found")

    def test_a_user_owning_nothing_reports_no_devices(self) -> None:
        status = snapshot_tray_status(self.graph())

        self.assertEqual(status.devices, ())

    def test_public_surface_is_exported(self) -> None:
        for name in (
            "build_tray_graph",
            "run_adoption",
            "snapshot_tray_status",
        ):
            self.assertTrue(callable(getattr(ui, name, None)), name)


if __name__ == "__main__":
    unittest.main()
