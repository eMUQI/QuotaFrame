import asyncio
import hashlib
import threading
import unittest
from dataclasses import replace
from types import SimpleNamespace
from unittest.mock import AsyncMock, Mock, patch

from quotaframe_bridge.i18n import tr
from quotaframe_bridge.ui.firmware_actions import FirmwareUpdateActions
from quotaframe_bridge.service.firmware_install import install_firmware
from quotaframe_bridge.service.ota import OtaService, UpgradeError
from quotaframe_bridge.service.multi_device import DeviceSession
from quotaframe_bridge.service.device_manager import DeviceManager
from quotaframe_bridge.sources.firmware_release import FirmwareImage, FirmwareManifest
from quotaframe_bridge.targets import TARGETS
from test_ota_service import FakeTransport, app_payload, status


class InstallationTests(unittest.IsolatedAsyncioTestCase):
    def setUp(self):
        self.link = FakeTransport([status(), status(), status("receiving"), status(version="0.5.0")])
        self.link.status = status()
        self.link.close = AsyncMock()
        self.manager = DeviceManager(lambda: self.link)
        self.manager._transport = self.link
        self.device = SimpleNamespace(address="AA", name="Panel", manager=self.manager,
            session=DeviceSession("Panel", self.manager), update_task=None)
        self.current = self.device
        self.graph = SimpleNamespace(get=lambda address: self.current)
        self.payload = app_payload(6000, TARGETS[0])
        self.image = FirmwareImage("m5sticks3", "0.5.0", len(self.payload),
            hashlib.sha256(self.payload).hexdigest(), "https://github.com/example/panel/releases/download/v0.5.0/app.bin")
        self.source = SimpleNamespace(fetch_manifest=AsyncMock(return_value=FirmwareManifest((self.image,))),
                                     download_image=AsyncMock(return_value=self.payload))
        self.confirm = AsyncMock(return_value=True)

    async def install(self):
        return await install_firmware(self.graph, self.device, "catalog", self.confirm,
                                      lambda value: None, source=self.source)

    async def test_download_and_desktop_confirmation_do_not_hold_exclusive_connection(self):
        async def download(image):
            self.assertFalse(self.device.session.exclusive_active)
            await self.manager.send_command(b'{"cmd":"usage"}', "usage", 1)
            return self.payload
        async def confirm(*args):
            self.assertFalse(self.device.session.exclusive_active)
            self.assertIs(args[2], self.image)
            return True
        self.source.download_image.side_effect = download
        self.confirm.side_effect = confirm
        result = await self.install()
        self.assertTrue(result.success)
        self.assertEqual(self.link.sent[0]["cmd"], "usage")
        self.assertIsNone(self.device.update_task)

    async def test_replacement_during_download_cannot_reuse_confirmation(self):
        async def download(image):
            self.current = SimpleNamespace(address="AA")
            return self.payload
        self.source.download_image.side_effect = download
        with self.assertRaisesRegex(UpgradeError, "changed"):
            await self.install()
        self.confirm.assert_not_awaited()
        self.assertEqual(self.link.sent, [])

    async def test_connection_replacement_during_confirmation_sends_no_abort(self):
        async def confirm(*args):
            self.manager._transport = FakeTransport([])
            return True
        self.confirm.side_effect = confirm
        with self.assertRaisesRegex(UpgradeError, "changed"):
            await self.install()
        self.assertEqual(self.link.sent, [])

    async def test_version_change_at_exclusive_preflight_sends_no_abort(self):
        self.link.statuses[1] = status(version="0.4.1")
        with self.assertRaisesRegex(UpgradeError, "changed"):
            await self.install()
        self.assertEqual(self.link.sent, [])

    async def test_cancelled_confirmation_sends_nothing(self):
        self.confirm.return_value = False
        self.assertIsNone(await self.install())
        self.assertEqual(self.link.sent, [])

    async def test_confirmed_install_waits_for_inflight_command_without_disconnect(self):
        from quotaframe_bridge.transports.bleak_nus import BleakNusTransport

        for command, task_field in (("time_sync", "_time_sync_task"),
                                    ("screen_page", "_screen_command_task")):
            with self.subTest(command=command):
                self.setUp()
                client = SimpleNamespace(
                    is_connected=True, mtu_size=247,
                    write_gatt_char=AsyncMock(), stop_notify=AsyncMock(),
                    disconnect=AsyncMock())
                link = BleakNusTransport(pairer=None)
                link._client = client
                link._rx = SimpleNamespace(max_write_without_response_size=244)
                link._tx = object()
                link.status = status()
                link.query_status = AsyncMock(return_value=link.status)
                self.manager._transport = link
                session = self.device.session
                pending = None

                async def confirm(*args):
                    nonlocal pending
                    async def send():
                        async with session._exclusive_lock:
                            await self.manager.send_command(
                                ('{"cmd":"%s","seq":"1"}\n' % command).encode(),
                                command, 1)
                    pending = asyncio.create_task(send())
                    setattr(session, task_field, pending)
                    while not client.write_gatt_char.await_count:
                        await asyncio.sleep(0)
                    return True

                self.confirm.side_effect = confirm
                upgrade = AsyncMock(return_value=SimpleNamespace(success=True))
                with patch.object(OtaService, "upgrade", upgrade):
                    install = asyncio.create_task(self.install())
                    try:
                        async with asyncio.timeout(1):
                            while not session.exclusive_active:
                                await asyncio.sleep(0)
                        await asyncio.sleep(0)
                        self.assertFalse(install.done())
                        self.assertFalse(pending.done())
                        client.disconnect.assert_not_awaited()
                        link._on_notification(None,
                            ('{"ack":"%s","n":1,"ok":true}\n' % command).encode())
                        self.assertTrue((await asyncio.wait_for(install, 1)).success)
                        upgrade.assert_awaited_once()
                        client.disconnect.assert_not_awaited()
                    finally:
                        install.cancel()
                        await asyncio.gather(install, return_exceptions=True)
                        await session.close()

    async def test_duplicate_click_is_rejected(self):
        self.device.update_task = asyncio.current_task()
        with self.assertRaisesRegex(UpgradeError, "already running"):
            await self.install()
        self.source.fetch_manifest.assert_not_awaited()


    async def test_menu_cancel_during_download_stops_without_device_commands(self):
        entered = asyncio.Event()
        async def download(image):
            entered.set()
            await asyncio.Event().wait()
        self.source.download_image.side_effect = download
        application = SimpleNamespace(_graph=self.graph, _set_firmware_action=Mock())
        task = asyncio.create_task(self.install())
        await asyncio.wait_for(entered.wait(), 1)
        await FirmwareUpdateActions._firmware_update_async(application, "AA", expected=self.device)
        await FirmwareUpdateActions._firmware_update_async(application, "AA", expected=self.device)
        with self.assertRaises(asyncio.CancelledError):
            await task
        self.assertEqual(task.cancelling(), 1)
        self.assertIsNone(self.device.update_task)
        self.assertFalse(self.device.update_cancellable)
        self.assertEqual(self.link.sent, [])

    async def test_menu_cancel_during_transfer_aborts_and_releases_exclusive_session(self):
        entered = asyncio.Event()
        send = self.link.send_command
        chunks = 0
        async def block_chunk(payload, command, *args):
            nonlocal chunks
            await send(payload, command, *args)
            if command == "chunk":
                chunks += 1
                if chunks == 2:
                    entered.set()
                    await asyncio.Event().wait()
        self.link.send_command = block_chunk
        application = SimpleNamespace(_graph=self.graph, _set_firmware_action=Mock())
        task = asyncio.create_task(self.install())
        await asyncio.wait_for(entered.wait(), 1)
        await FirmwareUpdateActions._firmware_update_async(application, "AA", expected=self.device)
        with self.assertRaises(asyncio.CancelledError):
            await task
        self.assertEqual(self.link.sent[-1]["cmd"], "ota_abort")
        self.assertFalse(self.device.session.exclusive_active)
        self.assertIsNone(self.device.update_task)

    async def test_final_chunk_disables_cancel_before_firmware_commit(self):
        observed = []
        application = SimpleNamespace(_graph=self.graph, _set_firmware_action=Mock())
        send = self.link.send_command
        async def inspect_send(payload, command, *args):
            if command == "char_end":
                observed.append(self.device.update_cancellable)
                await FirmwareUpdateActions._firmware_update_async(application, "AA", expected=self.device)
                self.assertFalse(asyncio.current_task().cancelling())
            await send(payload, command, *args)
        self.link.send_command = inspect_send
        result = await self.install()
        self.assertTrue(result.success)
        self.assertEqual(observed, [False])


    async def test_shared_ui_cancel_reports_completion_and_restores_action(self):
        entered = asyncio.Event()
        async def download(image):
            entered.set()
            await asyncio.Event().wait()
        self.source.download_image.side_effect = download
        application = FirmwareUpdateActions()
        application._graph = self.graph
        application._shutdown_lock = threading.Lock()
        application._stopping = False
        application._firmware_updates = 0
        application._firmware_manifest_url = lambda: "catalog"
        application._confirm_firmware = self.confirm
        application._set_firmware_action = Mock()
        application._notify_firmware = Mock()
        application._publish_firmware_actions = Mock()

        async def install(*args):
            return await install_firmware(*args, source=self.source)

        with patch("quotaframe_bridge.ui.firmware_actions.install_firmware", side_effect=install):
            task = asyncio.create_task(application._firmware_update_async("AA", expected=self.device))
            await asyncio.wait_for(entered.wait(), 1)
            await application._firmware_update_async("AA", expected=self.device)
            with self.assertRaises(asyncio.CancelledError):
                await task
        application._notify_firmware.assert_called_with(
            tr("firmware_cancelled"), tr("firmware_cancelled_body", label="Panel"))
        application._publish_firmware_actions.assert_called_once_with(self.graph)
        self.assertEqual(application._firmware_updates, 0)
