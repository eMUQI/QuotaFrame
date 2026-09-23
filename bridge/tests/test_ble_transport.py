from __future__ import annotations

import asyncio
import base64
import json
import re
import sys
import types
import unittest
from unittest.mock import patch

from quotaframe_bridge.pairing.base import (
    PairingBusyError,
    PairingError,
    PairingGuidanceError,
)
from quotaframe_bridge.protocol.messages import encode_usage
from quotaframe_bridge.transports.bleak_nus import (
    DEFAULT_NAME_PREFIXES,
    NUS_RX_UUID,
    NUS_TX_UUID,
    PAIRING_FAILURE_MESSAGE,
    BleakNusTransport,
    NusTransportError,
    _default_client_factory,
    chunk_payload,
    matches_device_name,
)
from quotaframe_bridge.transports.base import WritePolicy
from bridge.tests.test_protocol import sample_usage


class FakeCharacteristic:
    def __init__(self, uuid: str, write_size: int = 244) -> None:
        self.uuid = uuid
        self.max_write_without_response_size = write_size


class FakeServices:
    def __init__(self, rx: FakeCharacteristic, tx: FakeCharacteristic) -> None:
        self._characteristics = {rx.uuid: rx, tx.uuid: tx}

    def get_characteristic(self, uuid: str) -> FakeCharacteristic | None:
        return self._characteristics.get(uuid)


class FakeBleakClient:
    def __init__(
        self,
        *,
        acknowledge_usage: bool = True,
        connect_error: bool = False,
        write_size: int = 244,
        mtu_size: int = 100,
        target: str = "",
    ) -> None:
        self.rx = FakeCharacteristic(NUS_RX_UUID, write_size)
        self.tx = FakeCharacteristic(NUS_TX_UUID)
        self.services = FakeServices(self.rx, self.tx)
        self.mtu_size = mtu_size
        self.is_connected = False
        self.disconnect_calls = 0
        self.writes: list[bytes] = []
        self.responses: list[bool] = []
        self._incoming = bytearray()
        self._notify = None
        self._acknowledge_usage = acknowledge_usage
        self._connect_error = connect_error
        self._target = target
        self._folder_file_written = 0

    async def connect(self) -> None:
        if self._connect_error:
            raise RuntimeError("raw platform error")
        self.is_connected = True

    async def start_notify(self, characteristic, callback) -> None:
        self._notify = callback

    async def stop_notify(self, characteristic) -> None:
        self._notify = None

    async def write_gatt_char(self, characteristic, data, response: bool) -> None:
        self.assert_response = response
        self.responses.append(response)
        chunk = bytes(data)
        self.writes.append(chunk)
        self._incoming.extend(chunk)
        if b"\n" not in self._incoming:
            return
        line, _, tail = self._incoming.partition(b"\n")
        self._incoming = bytearray(tail)
        request = json.loads(line)
        if request["cmd"] == "status":
            reply = {
                "ack": "status", "n": 0,
                "ok": True,
                "data": {
                    "name": "M5 Usage Panel",
                    "sec": True,
                    "protocol": 1,
                    "page": "overview",
                    "caps": ['usage.v1'],
                    **({"target": self._target} if self._target else {}),
                },
            }
        elif self._acknowledge_usage:
            command = request["cmd"]
            if command in {"char_begin", "file", "char_end"}:
                if command == "file":
                    self._folder_file_written = 0
                ack_n = 0
            elif command == "chunk":
                self._folder_file_written += len(
                    base64.b64decode(request["d"], validate=True)
                )
                ack_n = self._folder_file_written
            elif command == "file_end":
                ack_n = self._folder_file_written
            else:
                ack_n = int(request["seq"])
            reply = {
                "ack": command,
                "ok": True,
                "n": ack_n,
            }
        else:
            return
        assert self._notify is not None
        self._notify(self.tx, (json.dumps(reply) + "\n").encode())

    async def disconnect(self) -> None:
        self.disconnect_calls += 1
        self.is_connected = False


class FakePairer:
    def __init__(self) -> None:
        self.devices: list[object] = []

    async def ensure_paired(self, device) -> None:
        self.devices.append(device)


class DeviceNameMatchingTests(unittest.TestCase):
    def test_default_prefixes_accept_both_maintained_targets(self) -> None:
        self.assertTrue(
            matches_device_name(
                "QF-M5-A1B2",
                exact_name=None,
                name_prefixes=DEFAULT_NAME_PREFIXES,
            )
        )
        self.assertTrue(
            matches_device_name(
                "QF-WS-S3-A216-C3D4",
                exact_name=None,
                name_prefixes=DEFAULT_NAME_PREFIXES,
            )
        )
        self.assertFalse(
            matches_device_name(
                "Other-Usage-C3D4",
                exact_name=None,
                name_prefixes=DEFAULT_NAME_PREFIXES,
            )
        )

    def test_exact_name_does_not_bypass_prefix_filter(self) -> None:
        self.assertFalse(
            matches_device_name(
                "QF-WS-S3-A216-C3D4",
                exact_name="QF-WS-S3-A216-C3D4",
                name_prefixes=("QF-M5-",),
            )
        )
        self.assertFalse(
            matches_device_name(
                "QF-WS-S3-A216-FFFF",
                exact_name="QF-WS-S3-A216-C3D4",
                name_prefixes=DEFAULT_NAME_PREFIXES,
            )
        )


class ChunkingTests(unittest.TestCase):
    def test_chunk_size_uses_smallest_limit(self) -> None:
        payload = bytes(range(250))

        chunks = chunk_payload(payload, reported_size=244, mtu=100)

        self.assertEqual([len(chunk) for chunk in chunks], [97, 97, 56])

    def test_conservative_cap_applies_when_adapter_reports_large_values(self) -> None:
        chunks = chunk_payload(b"x" * 181, reported_size=512, mtu=517)

        self.assertEqual([len(chunk) for chunk in chunks], [180, 1])

    def test_folder_push_policy_uses_512_but_respects_lower_limit(self) -> None:
        full = chunk_payload(
            b"x" * 513, reported_size=512, mtu=517,
            write_policy=WritePolicy.FOLDER_PUSH,
        )
        lower = chunk_payload(
            b"x" * 401, reported_size=300, mtu=517,
            write_policy=WritePolicy.FOLDER_PUSH,
        )

        self.assertEqual([len(chunk) for chunk in full], [512, 1])
        self.assertEqual([len(chunk) for chunk in lower], [300, 101])


class BleakNusTransportTests(unittest.IsolatedAsyncioTestCase):
    async def test_write_policy_changes_only_folder_push_chunks_and_response_mode(self) -> None:
        fake_client = FakeBleakClient(write_size=512, mtu_size=517)

        async def finder(*args, **kwargs):
            return object()

        transport = BleakNusTransport(
            pairer=FakePairer(),
            device_finder=finder,
            client_factory=lambda device, callback, timeout: fake_client,
            ack_timeout=0.1,
        )
        await transport.connect()

        fake_client.writes.clear()
        fake_client.responses.clear()
        await transport._write_line(b"x" * 181, WritePolicy.NORMAL)
        self.assertEqual([len(chunk) for chunk in fake_client.writes], [180, 1])
        self.assertEqual(fake_client.responses, [True, True])

        fake_client.writes.clear()
        fake_client.responses.clear()
        await transport._write_line(b"x" * 513, WritePolicy.FOLDER_PUSH)
        self.assertEqual([len(chunk) for chunk in fake_client.writes], [512, 1])
        self.assertEqual(fake_client.responses, [False, False])
        await transport.close()

    async def test_successful_connection_logs_validated_public_status(self) -> None:
        fake_client = FakeBleakClient()

        async def finder(*args, **kwargs):
            return object()

        transport = BleakNusTransport(
            pairer=FakePairer(),
            device_finder=finder,
            client_factory=lambda device, callback, timeout: fake_client,
            ack_timeout=0.1,
        )

        with self.assertLogs(
            "quotaframe_bridge.transports.bleak_nus",
            level="INFO",
        ) as captured:
            await transport.connect()

        record = "\n".join(captured.output)
        self.assertIn("BLE connected", record)
        self.assertIn("device=M5 Usage Panel", record)
        self.assertIn("secure=true", record)
        self.assertIn("protocol=1", record)
        self.assertIn("capabilities=usage.v1", record)
        await transport.close()

    async def test_connect_negotiates_status_then_sends_chunked_usage(self) -> None:
        fake_client = FakeBleakClient()

        async def finder(*args, **kwargs):
            return object()

        transport = BleakNusTransport(
            pairer=FakePairer(),
            device_finder=finder,
            client_factory=lambda device, callback, timeout: fake_client,
            ack_timeout=0.1,
        )
        await transport.connect()
        payload = encode_usage(sample_usage(), 42, 1_785_398_402)

        await transport.send_command(payload, "usage", 42)

        self.assertEqual(transport.status.name, "M5 Usage Panel")
        self.assertTrue(fake_client.assert_response)
        self.assertGreater(len(fake_client.writes), 2)
        await transport.close()
        self.assertFalse(fake_client.is_connected)

    async def test_ack_timeout_closes_connection_contract(self) -> None:
        fake_client = FakeBleakClient(acknowledge_usage=False)

        async def finder(*args, **kwargs):
            return object()

        transport = BleakNusTransport(
            pairer=FakePairer(),
            device_finder=finder,
            client_factory=lambda device, callback, timeout: fake_client,
            ack_timeout=0.01,
        )
        await transport.connect()

        with self.assertRaisesRegex(NusTransportError, "ACK timeout"):
            await transport.send_command(
                encode_usage(sample_usage(), 7, 1_785_398_402),
                "usage",
                7,
            )
        self.assertFalse(transport.connected)
        self.assertFalse(fake_client.is_connected)

    async def test_cancelled_command_requires_a_new_connection(self) -> None:
        class LateAckClient(FakeBleakClient):
            def __init__(self) -> None:
                super().__init__()
                self.time_sync_written = asyncio.Event()

            async def write_gatt_char(
                self,
                characteristic,
                data,
                response: bool,
            ) -> None:
                if b'"cmd":"time_sync"' in data:
                    self.writes.append(bytes(data))
                    self.responses.append(response)
                    self.time_sync_written.set()
                    return
                if b'"cmd":"usage"' in data:
                    assert self._notify is not None
                    late = {"ack": "time_sync", "ok": True, "n": 1}
                    self._notify(
                        self.tx,
                        (json.dumps(late) + "\n").encode(),
                    )
                await super().write_gatt_char(characteristic, data, response)

        fake_client = LateAckClient()

        async def finder(*args, **kwargs):
            return object()

        transport = BleakNusTransport(
            pairer=FakePairer(),
            device_finder=finder,
            client_factory=lambda device, callback, timeout: fake_client,
            ack_timeout=0.1,
        )
        await transport.connect()

        cancelled = asyncio.create_task(
            transport.send_command(
                b'{"cmd":"time_sync","seq":"1"}\n',
                "time_sync",
                1,
            )
        )
        await fake_client.time_sync_written.wait()
        cancelled.cancel()
        await asyncio.gather(cancelled, return_exceptions=True)
        self.assertFalse(transport.connected)
        self.assertFalse(fake_client.is_connected)
        with self.assertRaises(NusTransportError):
            await transport.send_command(b"unused", "usage", 2)
        transport._client_factory = lambda *args: FakeBleakClient()
        await transport.connect()


        await transport.send_command(
            encode_usage(sample_usage(), 2, 1_785_398_402),
            "usage",
            2,
        )
        await transport.close()

    async def test_close_releases_client_after_remote_disconnect(self) -> None:
        fake_client = FakeBleakClient()

        async def finder(*args, **kwargs):
            return object()

        transport = BleakNusTransport(
            pairer=FakePairer(),
            device_finder=finder,
            client_factory=lambda device, callback, timeout: fake_client,
            ack_timeout=0.1,
        )
        await transport.connect()
        fake_client.is_connected = False

        await transport.close()

        self.assertEqual(fake_client.disconnect_calls, 1)

    async def test_disconnect_during_write_has_no_orphaned_future(self) -> None:
        class DisconnectingWriteClient(FakeBleakClient):
            disconnected_callback = None

            async def write_gatt_char(
                self,
                characteristic,
                data,
                response: bool,
            ) -> None:
                if b'"cmd":"usage"' in data:
                    self.is_connected = False
                    assert self.disconnected_callback is not None
                    self.disconnected_callback(self)
                    raise OSError("link lost")
                await super().write_gatt_char(
                    characteristic,
                    data,
                    response,
                )

        fake_client = DisconnectingWriteClient()

        async def finder(*args, **kwargs):
            return object()

        def factory(device, callback, timeout):
            fake_client.disconnected_callback = callback
            return fake_client

        transport = BleakNusTransport(
            pairer=FakePairer(),
            device_finder=finder,
            client_factory=factory,
            ack_timeout=0.1,
        )
        await transport.connect()
        loop = asyncio.get_running_loop()
        contexts: list[dict[str, object]] = []
        previous_handler = loop.get_exception_handler()
        loop.set_exception_handler(lambda _loop, context: contexts.append(context))
        try:
            with self.assertRaisesRegex(NusTransportError, "command failed"):
                await transport.send_command(
                    encode_usage(sample_usage(), 7, 1_785_398_402),
                    "usage",
                    7,
                )
            await asyncio.sleep(0)
        finally:
            loop.set_exception_handler(previous_handler)
            await transport.close()

        self.assertEqual(contexts, [])

    async def test_missing_scan_result_is_reported_without_bleak_details(self) -> None:
        async def finder(*args, **kwargs):
            return None

        transport = BleakNusTransport(
            pairer=FakePairer(),
            device_finder=finder,
        )

        with self.assertRaisesRegex(NusTransportError, "not found"):
            await transport.connect()

    async def test_pairing_uses_configured_connect_timeout(self) -> None:
        fake_client = FakeBleakClient()
        captured: list[float] = []

        async def finder(*args, **kwargs):
            return object()

        def factory(device, callback, timeout):
            captured.append(timeout)
            return fake_client

        transport = BleakNusTransport(
            pairer=FakePairer(),
            device_finder=finder,
            client_factory=factory,
            connect_timeout=75,
            ack_timeout=0.1,
        )
        await transport.connect()

        self.assertEqual(captured, [75])
        await transport.close()

    async def test_connect_pairs_before_creating_gatt_client(self) -> None:
        events: list[str] = []
        device = object()
        fake_client = FakeBleakClient()

        async def finder(*args, **kwargs):
            return device

        class RecordingPairer:
            async def ensure_paired(self, actual) -> None:
                if actual is not device:
                    raise AssertionError("pairer received the wrong device")
                events.append("paired")

        def factory(actual, callback, timeout):
            if actual is not device:
                raise AssertionError("client factory received the wrong device")
            events.append("client")
            return fake_client

        transport = BleakNusTransport(
            pairer=RecordingPairer(),
            device_finder=finder,
            client_factory=factory,
            ack_timeout=0.1,
        )

        await transport.connect()

        self.assertEqual(events, ["paired", "client"])
        await transport.close()

    async def test_pairing_failure_does_not_create_gatt_client(self) -> None:
        client_created = False

        async def finder(*args, **kwargs):
            return object()

        class FailingPairer:
            async def ensure_paired(self, device) -> None:
                raise PairingError("sensitive platform detail")

        def factory(device, callback, timeout):
            nonlocal client_created
            client_created = True
            return FakeBleakClient()

        transport = BleakNusTransport(
            pairer=FailingPairer(),
            device_finder=finder,
            client_factory=factory,
        )

        with self.assertRaisesRegex(
            NusTransportError,
            f"^{re.escape(PAIRING_FAILURE_MESSAGE)}$",
        ):
            await transport.connect()

        self.assertFalse(client_created)

    async def test_pairer_guidance_reaches_the_user_verbatim(self) -> None:
        guidance = "open System Settings > Bluetooth and Forget This Device"

        async def finder(*args, **kwargs):
            return object()

        class GuidingPairer:
            async def ensure_paired(self, device) -> None:
                raise PairingGuidanceError(guidance)

        transport = BleakNusTransport(
            pairer=GuidingPairer(),
            device_finder=finder,
            client_factory=lambda device, callback, timeout: (
                self.fail("guided pairing failure created a GATT client")
            ),
        )

        with self.assertRaises(NusTransportError) as caught:
            await transport.connect()

        self.assertEqual(str(caught.exception), guidance)

    async def test_pairing_busy_guidance_is_preserved(self) -> None:
        guidance = (
            "Windows pairing for QF-M5-D86A is still in progress; "
            "close Add a device, stop other Bridge instances, then retry "
            "the scoped pairing repair"
        )

        async def finder(*args, **kwargs):
            return object()

        class BusyPairer:
            async def ensure_paired(self, device) -> None:
                raise PairingBusyError(guidance)

        transport = BleakNusTransport(
            pairer=BusyPairer(),
            device_finder=finder,
            client_factory=lambda device, callback, timeout: (
                self.fail("busy pairing created a GATT client")
            ),
        )

        with self.assertRaises(NusTransportError) as caught:
            await transport.connect()

        self.assertEqual(str(caught.exception), guidance)

    async def test_gatt_failure_includes_explicit_repair_instruction(self) -> None:
        fake_client = FakeBleakClient(connect_error=True)

        async def finder(*args, **kwargs):
            return object()

        transport = BleakNusTransport(
            pairer=FakePairer(),
            device_finder=finder,
            client_factory=lambda device, callback, timeout: fake_client,
        )

        with self.assertRaisesRegex(
            NusTransportError,
            "--repair-pairing",
        ):
            await transport.connect()


class FakeScannedDevice:
    """The subset of a Bleak BLEDevice the finder and adoption flow read."""

    def __init__(self, address: str, name: str = "QF-M5-A1B2") -> None:
        self.address = address
        self.name = name


class AddressDiscoveryTests(unittest.IsolatedAsyncioTestCase):
    """An adopted device is found by address, never by advertising name."""

    async def test_name_and_nus_advertisements_accumulate_per_address(self):
        from types import SimpleNamespace
        from quotaframe_bridge.transports.bleak_nus import _default_device_finder
        device = FakeScannedDevice("AA", name=None)
        other = FakeScannedDevice("BB", name=None)
        uuid = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
        owner = self
        class Scanner:
            @staticmethod
            async def find_device_by_filter(predicate, timeout):
                owner.assertFalse(predicate(device, SimpleNamespace(local_name="QF-Custom", service_uuids=[])))
                owner.assertFalse(predicate(other, SimpleNamespace(local_name=None, service_uuids=[uuid])))
                owner.assertTrue(predicate(device, SimpleNamespace(local_name=None, service_uuids=[uuid])))
                return device
        module = types.ModuleType("bleak")
        module.BleakScanner = Scanner
        with patch.dict(sys.modules, {"bleak": module}):
            found = await _default_device_finder(exact_name=None, name_prefixes=("QF-",),
                                               service_uuid=uuid, timeout=.1)
        self.assertIs(found, device)

    async def test_saved_address_is_passed_to_the_finder_normalized(self) -> None:
        captured: dict[str, object] = {}

        async def finder(**kwargs):
            captured.update(kwargs)
            return FakeScannedDevice("aa:bb:cc:dd:ee:ff")

        transport = BleakNusTransport(
            pairer=FakePairer(),
            address="aa:bb:cc:dd:ee:ff",
            device_finder=finder,
            client_factory=lambda device, callback, timeout: FakeBleakClient(),
            ack_timeout=0.1,
        )
        await transport.connect()

        self.assertEqual(captured["address"], "AA:BB:CC:DD:EE:FF")
        await transport.close()

    async def test_scanned_address_is_recorded_for_adoption(self) -> None:
        async def finder(**kwargs):
            return FakeScannedDevice("aa:bb:cc:dd:ee:ff")

        transport = BleakNusTransport(
            pairer=FakePairer(),
            device_finder=finder,
            client_factory=lambda device, callback, timeout: FakeBleakClient(),
            ack_timeout=0.1,
        )
        await transport.connect()

        self.assertEqual(transport.found_address, "AA:BB:CC:DD:EE:FF")
        await transport.close()

    async def test_address_mode_ignores_the_advertising_name_entirely(self) -> None:
        from quotaframe_bridge.transports.bleak_nus import (
            _default_device_finder,
        )

        renamed = FakeScannedDevice("AA:BB:CC:DD:EE:FF", name="Lab-Renamed-Panel")
        captured: dict[str, object] = {}

        class FakeScanner:
            @staticmethod
            async def find_device_by_filter(predicate, timeout):
                captured["matched"] = predicate(renamed, object())
                return renamed if captured["matched"] else None

        module = types.ModuleType("bleak")
        module.BleakScanner = FakeScanner
        with patch.dict(sys.modules, {"bleak": module}):
            found = await _default_device_finder(
                exact_name=None,
                name_prefixes=DEFAULT_NAME_PREFIXES,
                service_uuid="6e400001-b5a3-f393-e0a9-e50e24dcca9e",
                timeout=0.1,
                address="aa:bb:cc:dd:ee:ff",
            )

        self.assertIs(found, renamed)
        self.assertTrue(captured["matched"])

    async def test_address_mode_rejects_a_different_board(self) -> None:
        from quotaframe_bridge.transports.bleak_nus import (
            _default_device_finder,
        )

        stranger = FakeScannedDevice("AA:BB:CC:DD:EE:00")

        class FakeScanner:
            @staticmethod
            async def find_device_by_filter(predicate, timeout):
                return stranger if predicate(stranger, object()) else None

        module = types.ModuleType("bleak")
        module.BleakScanner = FakeScanner
        with patch.dict(sys.modules, {"bleak": module}):
            found = await _default_device_finder(
                exact_name=None,
                name_prefixes=DEFAULT_NAME_PREFIXES,
                service_uuid="6e400001-b5a3-f393-e0a9-e50e24dcca9e",
                timeout=0.1,
                address="AA:BB:CC:DD:EE:FF",
            )

        self.assertIsNone(found)


class IdentityVerificationTests(unittest.IsolatedAsyncioTestCase):
    """Status negotiation and callback identity do not depend on the target registry."""

    async def _connect(self, *, reported: str, expected: str | None):
        fake_client = FakeBleakClient(target=reported)

        async def finder(**kwargs):
            return FakeScannedDevice("AA:BB:CC:DD:EE:FF")

        transport = BleakNusTransport(
            pairer=FakePairer(),
            device_finder=finder,
            client_factory=lambda device, callback, timeout: fake_client,
            ack_timeout=0.1,
        )
        return transport, fake_client

    async def test_old_client_notification_and_disconnect_cannot_change_new_client(self):
        clients = [FakeBleakClient(), FakeBleakClient()]
        async def finder(**kwargs):
            return FakeScannedDevice("AA")
        transport = BleakNusTransport(pairer=FakePairer(), device_finder=finder,
            client_factory=lambda *args: clients.pop(0), ack_timeout=.1)
        await transport.connect()
        old = transport._client
        callback = old._notify
        await transport.close()
        await transport.connect()
        current_status = transport.status
        transport._status_future = asyncio.get_running_loop().create_future()
        callback(None, b'{"ack":"status","ok":false,"n":0}\n')
        transport._on_disconnected(old)
        self.assertFalse(transport._status_future.done())
        self.assertIs(transport.status, current_status)
        transport._status_future.cancel()
        await transport.close()

    async def test_matching_target_connects(self) -> None:
        transport, _ = await self._connect(
            reported="m5sticks3", expected="m5sticks3"
        )

        await transport.connect()

        self.assertEqual(transport.status.target, "m5sticks3")
        await transport.close()

    async def test_changed_target_does_not_block_usage(self) -> None:
        transport, fake_client = await self._connect(
            reported="waveshare_amoled_216", expected="m5sticks3"
        )

        await transport.connect()
        self.assertTrue(fake_client.is_connected)
        await transport.close()

    async def test_device_without_target_connects(self):
        transport, _ = await self._connect(reported="", expected=None)
        await transport.connect()
        self.assertEqual(transport.status.target, "")
        await transport.close()

    async def test_no_expected_target_keeps_adoption_scans_open(self) -> None:
        transport, _ = await self._connect(reported="m5sticks3", expected=None)

        await transport.connect()

        self.assertEqual(transport.status.target, "m5sticks3")
        await transport.close()


class DefaultClientFactoryTests(unittest.TestCase):
    def _capture_client_arguments(self, platform: str) -> dict[str, object]:
        captured: dict[str, object] = {}

        def fake_client(device, **kwargs):
            captured.update(kwargs)
            return object()

        fake_bleak = types.ModuleType("bleak")
        fake_bleak.BleakClient = fake_client

        with (
            patch.dict(sys.modules, {"bleak": fake_bleak}),
            patch("quotaframe_bridge.transports.bleak_nus.sys.platform", platform),
        ):
            _default_client_factory(object(), self._disconnected, 75)
        return captured

    @staticmethod
    def _disconnected(_client) -> None:
        return None

    def test_bleak_client_is_created_without_implicit_pairing(self) -> None:
        captured = self._capture_client_arguments("win32")

        self.assertIs(captured["disconnected_callback"], self._disconnected)
        self.assertIs(captured["pair"], False)
        self.assertEqual(captured["timeout"], 75)

    def test_bleak_client_uses_fresh_windows_gatt_services(self) -> None:
        captured = self._capture_client_arguments("win32")

        self.assertEqual(
            captured.get("winrt"),
            {"use_cached_services": False},
        )

    def test_non_windows_backends_receive_no_winrt_options(self) -> None:
        # CoreBluetooth has no service cache to invalidate, and Bleak's macOS
        # backend rejects options it does not know.
        captured = self._capture_client_arguments("darwin")

        self.assertNotIn("winrt", captured)


if __name__ == "__main__":
    unittest.main()
