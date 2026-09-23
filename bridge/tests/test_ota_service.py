from __future__ import annotations

import base64
import asyncio
import hashlib
import json
import unittest
import struct
from pathlib import Path
from dataclasses import replace

from quotaframe_bridge.protocol.messages import DeviceStatus
from quotaframe_bridge.protocol.ota_messages import OtaStatus, VALID_ERRORS
from quotaframe_bridge.service.ota import OtaService, UpgradeError
from quotaframe_bridge.sources.firmware_release import FirmwareImage
from quotaframe_bridge.targets import TARGETS
from quotaframe_bridge.transports.base import WritePolicy


def app_payload(size, target, version="0.5.0"):
    payload = bytearray(size)
    payload[0] = 0xE9
    struct.pack_into("<H", payload, 12, target.image_chip_id)
    struct.pack_into("<I", payload, 32, 0xABCD5432)
    for offset, text in ((48, version), (80, Path(target.ota_image).stem)):
        payload[offset:offset + len(text)] = text.encode("ascii")
    return bytes(payload)


def status(
    phase: str = "idle",
    *,
    error: str = "",
    version: str = "0.4.0",
    target: str = "m5sticks3",
    offset: int = 0,
    size: int = 0,
) -> DeviceStatus:
    return DeviceStatus(
        name="M5 Usage Panel",
        boot_valid=True,
        secure=True,
        protocol=1,
        page="overview",
        capabilities=frozenset({"usage.v1", "ota.folder.v1"}),
        firmware_version=version,
        firmware_project="quotaframe",
        target=target,
        ota=OtaStatus(phase=phase, offset=offset, size=size, error=error),
    )


class FakeTransport:
    def __init__(self, statuses: list[DeviceStatus]) -> None:
        self.statuses = list(statuses)
        self.sent: list[dict[str, object]] = []
        self.connect_calls = 0
        self.connected = True
        self.fail_command: str | None = None

    async def query_status(self) -> DeviceStatus:
        if not self.statuses:
            raise ConnectionError("private adapter detail")
        return self.statuses.pop(0)

    async def send_command(
        self, payload: bytes, command: str, expected_ack_n: int,
        write_policy: WritePolicy = WritePolicy.NORMAL,
    ) -> None:
        message = json.loads(payload)
        if message["cmd"] != command:
            raise AssertionError("declared command does not match payload")
        message["_expected_ack_n"] = expected_ack_n
        message["_write_policy"] = write_policy
        self.sent.append(message)
        # The pre-transfer cleanup abort tears down no session, so it must
        # leave intact the error a later transfer failure would otherwise
        # report; only an abort after the transfer has started clears the
        # device's retained error.
        if command == "ota_abort" and len(self.sent) > 1:
            for index, pending in enumerate(self.statuses):
                if pending.ota is not None:
                    self.statuses[index] = replace(
                        pending, ota=replace(pending.ota, error="")
                    )
        if command == self.fail_command:
            raise ConnectionError("private adapter detail")

    async def connect(self) -> None:
        self.connect_calls += 1


class OtaServiceTests(unittest.IsolatedAsyncioTestCase):
    def setUp(self) -> None:
        self.payload = app_payload(6000, TARGETS[0])
        self.image = FirmwareImage(
            target="m5sticks3",
            version="0.5.0",
            size=len(self.payload),
            sha256=hashlib.sha256(self.payload).hexdigest(),
            url="https://github.com/example/panel/releases/download/v0.5.0/m5.bin",
        )

    def test_payload_limit_uses_each_targets_app_partition(self) -> None:
        for target in TARGETS:
            for excess in (0, 1):
                with self.subTest(target=target.id, excess=excess):
                    payload = app_payload(target.ota_partition_bytes + excess, target)
                    image = replace(
                        self.image, target=target.id, size=len(payload),
                        sha256=hashlib.sha256(payload).hexdigest(),
                    )
                    if excess:
                        with self.assertRaisesRegex(UpgradeError, "OTA transfer limit"):
                            OtaService._validate_payload(image, payload)
                    else:
                        OtaService._validate_payload(image, payload)

    async def test_success_sends_manifest_and_firmware_then_observes_new_version(self) -> None:
        transport = FakeTransport(
            [
                status(),
                status("confirming", size=len(self.payload)),
                status("receiving", size=len(self.payload)),
                status(version="0.5.0"),
            ]
        )
        progress: list[tuple[int, int]] = []
        service = OtaService(sleep=lambda _delay: _completed_sleep())

        with self.assertLogs(
            "quotaframe_bridge.service.ota", level="INFO"
        ) as captured:
            result = await service.upgrade(
                transport, self.image, self.payload, progress.append
            )

        rendered_log = "\n".join(captured.output)
        self.assertIn("firmware transfer complete", rendered_log)
        self.assertIn("firmware verification complete", rendered_log)
        self.assertIn("updated device returned", rendered_log)
        self.assertTrue(result.success)
        self.assertEqual(
            [message["cmd"] for message in transport.sent],
            [
                "ota_abort",
                "char_begin",
                "file",
                "chunk",
                "file_end",
                "file",
                "chunk",
                "chunk",
                "chunk",
                "file_end",
                "char_end",
            ],
        )
        manifest = base64.b64decode(transport.sent[3]["d"], validate=True)
        self.assertEqual(
            json.loads(manifest),
            {
                "schema": "1",
                "firmware_project": "quotaframe",
                "sha256": self.image.sha256,
                "size": len(self.payload),
                "target": "m5sticks3",
                "version": "0.5.0",
            },
        )
        firmware_chunks = transport.sent[6:9]
        self.assertEqual(
            transport.sent[3]["_expected_ack_n"], len(manifest)
        )
        self.assertEqual(
            transport.sent[4]["_expected_ack_n"], len(manifest)
        )
        self.assertEqual(
            [message["_expected_ack_n"] for message in firmware_chunks],
            [2880, 5760, 6000],
        )
        self.assertTrue(
            all(
                message["_write_policy"] is WritePolicy.FOLDER_PUSH
                for message in [transport.sent[3], *firmware_chunks]
            )
        )
        self.assertIs(transport.sent[0]["_write_policy"], WritePolicy.NORMAL)
        self.assertIs(transport.sent[-1]["_write_policy"], WritePolicy.NORMAL)
        self.assertEqual(progress[-1], (len(self.payload), len(self.payload)))

    async def test_preflight_rejects_busy_or_wrong_target(self) -> None:
        for initial in (status("receiving"), status(target="waveshare_amoled_216")):
            with self.subTest(initial=initial):
                with self.assertRaises(UpgradeError):
                    await OtaService().upgrade(
                        FakeTransport([initial]), self.image, self.payload
                    )

    async def test_preflight_rejects_same_or_older_release_version(self) -> None:
        for current in ("0.5.0", "0.6.0"):
            transport = FakeTransport([status(version=current)])
            with self.subTest(current=current), self.assertRaisesRegex(
                UpgradeError, "newer"
            ):
                await OtaService().upgrade(
                    transport,
                    self.image,
                    self.payload,
                )
            self.assertEqual(transport.sent, [])

    async def test_preflight_accepts_newer_release_for_git_describe_device(self) -> None:
        transport = FakeTransport(
            [
                status(version="v0.4.0-26-g921badd"),
                status("confirming", size=len(self.payload)),
                status("receiving", size=len(self.payload)),
                status(version="0.5.0"),
            ]
        )

        result = await OtaService(
            sleep=lambda _delay: _completed_sleep()
        ).upgrade(transport, self.image, self.payload)

        self.assertTrue(result.success)

    async def test_preflight_does_not_replace_same_git_describe_release(self) -> None:
        transport = FakeTransport([status(version="v0.5.0-26-g921badd")])

        with self.assertRaisesRegex(UpgradeError, "newer"):
            await OtaService().upgrade(transport, self.image, self.payload)

        self.assertEqual(transport.sent, [])

    async def test_preflight_rejects_non_semver_versions(self) -> None:
        cases = (
            (replace(self.image, version="release"), status()),
            (self.image, status(version="development")),
        )
        for image, current in cases:
            transport = FakeTransport([current])
            with self.subTest(image=image, current=current), self.assertRaises(
                UpgradeError
            ):
                await OtaService().upgrade(transport, image, self.payload)
            self.assertEqual(transport.sent, [])

    async def test_confirmation_denial_timeout_and_link_loss_are_sanitized(self) -> None:
        cases = [
            [status(), status("idle", error="denied")],
            [status(), status("confirming"), status("confirming")],
            [status(), status("confirming")],
        ]
        for index, statuses in enumerate(cases):
            with self.subTest(index=index):
                service = OtaService(
                    confirm_timeout=0.5 if index == 1 else 60,
                    sleep=lambda _delay: _completed_sleep(),
                )
                with self.assertRaises(UpgradeError) as captured:
                    await service.upgrade(
                        FakeTransport(statuses), self.image, self.payload
                    )
                self.assertNotIn("private adapter detail", str(captured.exception))

    async def test_ack_failure_bad_image_and_reboot_timeout_fail(self) -> None:
        ack_transport = FakeTransport([status(), status("receiving")])
        ack_transport.fail_command = "chunk"
        with self.assertRaises(UpgradeError):
            await OtaService(sleep=lambda _delay: _completed_sleep()).upgrade(
                ack_transport, self.image, self.payload
            )
        self.assertEqual(ack_transport.sent[-1]["cmd"], "ota_abort")
        self.assertEqual(
            ack_transport.sent[-1]["_expected_ack_n"],
            int(ack_transport.sent[-1]["seq"]),
        )

        bad_image = FakeTransport(
            [status(), status("receiving"), status("idle", error="bad_image")]
        )
        with self.assertRaises(UpgradeError):
            await OtaService(sleep=lambda _delay: _completed_sleep()).upgrade(
                bad_image, self.image, self.payload
            )

        reboot_timeout = FakeTransport([status(), status("receiving"), status()])
        with self.assertRaises(UpgradeError):
            await OtaService(
                reboot_timeout=0,
                sleep=lambda _delay: _completed_sleep(),
            ).upgrade(reboot_timeout, self.image, self.payload)

    async def test_manifest_rejection_surfaces_retained_device_error(self) -> None:
        transport = FakeTransport(
            [
                status(),
                status(error="low_power"),
            ]
        )
        transport.fail_command = "file_end"

        with self.assertRaises(UpgradeError) as caught:
            await OtaService(
                sleep=lambda _delay: _completed_sleep()
            ).upgrade(transport, self.image, self.payload)

        self.assertEqual(
            str(caught.exception),
            "device battery too low - plug in or charge before updating",
        )
        aborts = [
            message for message in transport.sent if message["cmd"] == "ota_abort"
        ]
        self.assertEqual(len(aborts), 2)

    async def test_transfer_failure_keeps_original_error_when_link_is_dead(self) -> None:
        transport = FakeTransport([status()])
        transport.fail_command = "file_end"

        with self.assertRaises(UpgradeError) as caught:
            await OtaService(
                sleep=lambda _delay: _completed_sleep()
            ).upgrade(transport, self.image, self.payload)

        self.assertEqual(str(caught.exception), "connection lost during file_end; command outcome is unknown")


class OtaLifecycleTests(unittest.IsolatedAsyncioTestCase):
    def image(self):
        payload = app_payload(6000, TARGETS[0])
        return FirmwareImage("m5sticks3", "0.5.0", len(payload),
                             hashlib.sha256(payload).hexdigest(),
                             "https://github.com/example/panel/releases/download/v0.5.0/m5.bin"), payload

    async def test_transfer_link_loss_releases_exclusive_and_allows_retry(self):
        from quotaframe_bridge.service.device_manager import DeviceManager
        from quotaframe_bridge.service.multi_device import DeviceSession
        from unittest.mock import AsyncMock

        transport = FakeTransport([status(), status("receiving")])
        transport.close = AsyncMock()
        transport.fail_command = "chunk"
        offline = FakeTransport([])
        offline.connect = AsyncMock(side_effect=ConnectionError("offline"))
        offline.close = AsyncMock()
        manager = DeviceManager(lambda: offline)
        manager._transport = transport
        session = DeviceSession("panel", manager)
        image, payload = self.image()
        ota = OtaService(upgrade_timeout=.2, cleanup_timeout=.01)
        with self.assertRaises(UpgradeError):
            await asyncio.wait_for(session.run_exclusive(
                lambda link: ota.upgrade(link, image, payload)), .5)
        self.assertFalse(session.exclusive_active)
        self.assertIsNone(manager._transport)

        recovered = FakeTransport([status(), status("receiving"), status(version="0.5.0")])
        recovered.close = AsyncMock()
        manager._transport = recovered
        result = await session.run_exclusive(lambda link: ota.upgrade(link, image, payload))
        self.assertTrue(result.success)
        await session.close()

    async def test_close_cancels_and_awaits_an_independent_exclusive_task(self):
        from quotaframe_bridge.service.device_manager import DeviceManager
        from quotaframe_bridge.service.multi_device import DeviceSession
        from unittest.mock import AsyncMock

        transport = FakeTransport([])
        transport.close = AsyncMock()
        manager = DeviceManager(lambda: transport)
        manager._transport = transport
        session = DeviceSession("panel", manager)
        started = asyncio.Event()
        cleaned = asyncio.Event()
        async def downloading(link):
            started.set()
            try:
                await asyncio.Event().wait()
            finally:
                await asyncio.sleep(0)
                cleaned.set()
        task = asyncio.create_task(session.run_exclusive(downloading))
        await asyncio.wait_for(started.wait(), .5)
        await asyncio.wait_for(session.close(), .5)
        self.assertTrue(task.cancelled())
        self.assertTrue(cleaned.is_set())
        self.assertFalse(session.exclusive_active)
        self.assertFalse(manager.connected)
        with self.assertRaises(RuntimeError):
            await session.run_exclusive(downloading)

    async def test_reboot_status_reconnect_is_bounded(self):
        from quotaframe_bridge.service.device_manager import DeviceManager
        from unittest.mock import AsyncMock
        offline = FakeTransport([])
        offline.connect = AsyncMock(side_effect=ConnectionError("offline"))
        offline.close = AsyncMock()
        manager = DeviceManager(lambda: offline)
        with self.assertRaisesRegex(UpgradeError, "confirm startup"):
            await asyncio.wait_for(OtaService(reboot_timeout=.01)._wait_for_reboot(manager, self.image()[0]), .5)

    async def test_link_failure_does_not_reconnect_for_cleanup(self):
        from quotaframe_bridge.service.device_manager import DeviceManager
        from unittest.mock import AsyncMock
        first = FakeTransport([status()])
        first.close = AsyncMock()
        first.fail_command = "file_end"
        factory = AsyncMock()
        manager = DeviceManager(factory)
        manager._transport = first
        image, payload = self.image()
        with self.assertRaises(UpgradeError):
            await OtaService().upgrade(manager, image, payload)
        factory.assert_not_called()
        self.assertIsNone(manager.current_transport)

    async def test_version_alone_is_not_success(self):
        transport = FakeTransport([
            replace(status(version="0.5.0"), boot_valid=False),
            status(version="0.5.0"),
        ])
        await OtaService(poll_interval=.001)._wait_for_reboot(transport, self.image()[0])
        self.assertEqual(transport.statuses, [])

    async def test_negative_folder_ack_is_delivered_without_resetting_link(self):
        from quotaframe_bridge.protocol.messages import CommandRejected
        from quotaframe_bridge.service.device_manager import DeviceManager
        from quotaframe_bridge.transports.bleak_nus import BleakNusTransport
        from unittest.mock import AsyncMock, Mock
        for offset in (0, 1440):
            link = BleakNusTransport(pairer=None)
            link._client = Mock(is_connected=True)
            link._rx = Mock()
            async def reject(*args):
                link._on_notification(None, ('{"ack":"chunk","ok":false,"n":%d,"error":"bad_sequence"}\n' % offset).encode())
            link._write_line = AsyncMock(side_effect=reject)
            link.close = AsyncMock()
            manager = DeviceManager(lambda: link)
            manager._transport = link
            with self.assertRaisesRegex(CommandRejected, "bad_sequence"):
                await manager.send_command(b"chunk", "chunk", 2880)
            self.assertIs(manager._transport, link)
            link.close.assert_not_awaited()
            self.assertIsNone(link._ack_future)
            self.assertIsNone(link._expected_ack)

    async def test_folder_ack_keeps_success_offsets_and_rejection_values_strict(self):
        import json
        from quotaframe_bridge.protocol.messages import AckError, parse_ack
        from quotaframe_bridge.transports.bleak_nus import BleakNusTransport
        for ok, offset in ((True, 1440), (False, -1), (False, True), (False, 1.5), (False, 0x100000000)):
            message = {"ack": "chunk", "ok": ok, "n": offset}
            link = BleakNusTransport(pairer=None)
            link._expected_ack = ("chunk", 2880)
            link._ack_future = asyncio.get_running_loop().create_future()
            try:
                link._on_notification(None, (json.dumps(message) + "\n").encode())
                if offset == -1:
                    self.assertIsNotNone(link._ack_future.exception())
                else:
                    self.assertFalse(link._ack_future.done())
                with self.assertRaises(AckError):
                    parse_ack(message, "chunk", 2880)
            finally:
                link._ack_future.cancel()

    async def test_total_deadline_cancels_a_stalled_transfer(self):
        from quotaframe_bridge.service.multi_device import DeviceSession
        from unittest.mock import AsyncMock
        transport = FakeTransport([status()])
        transport.close = AsyncMock()
        async def stalled(*args):
            await asyncio.Event().wait()
        transport.send_command = stalled
        session = DeviceSession("panel", transport)
        image, payload = self.image()
        with self.assertRaisesRegex(UpgradeError, "timed out"):
            await asyncio.wait_for(session.run_exclusive(lambda link: OtaService(
                upgrade_timeout=.01, cleanup_timeout=.01).upgrade(link, image, payload)), .5)
        self.assertFalse(session.exclusive_active)


async def _completed_sleep() -> None:
    return None


class PublicErrorTextTests(unittest.TestCase):
    def test_low_power_maps_to_an_actionable_message(self) -> None:
        message = OtaService._public_error("low_power")

        self.assertEqual(
            message, "device battery too low - plug in or charge before updating"
        )

    def test_unknown_errors_keep_the_generic_fallback(self) -> None:
        self.assertEqual(
            OtaService._public_error("mystery"), "firmware update failed"
        )

    def test_every_wire_error_word_has_user_facing_text(self) -> None:
        generic = OtaService._public_error("mystery")
        for word in sorted(VALID_ERRORS - {""}):
            with self.subTest(word=word):
                self.assertNotEqual(
                    OtaService._public_error(word),
                    generic,
                    f"wire error {word!r} has no dedicated user-facing text",
                )


if __name__ == "__main__":
    unittest.main()
