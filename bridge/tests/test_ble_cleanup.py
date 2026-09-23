from __future__ import annotations

import asyncio
import unittest
from unittest.mock import AsyncMock, Mock, patch

from quotaframe_bridge.service.device_manager import DeviceManager
from quotaframe_bridge.service.multi_device import DeviceSession
from quotaframe_bridge.transports import bleak_nus
from quotaframe_bridge.transports.base import TransportCleanupError
from quotaframe_bridge.ui.status import DeviceStatus, PanelStatus, TrayState, format_device_summary


class CleanupDeadlineTests(unittest.IsolatedAsyncioTestCase):
    def setUp(self):
        for name, value in (("_STOP_NOTIFY_TIMEOUT", 0.02), ("_DISCONNECT_TIMEOUT", 0.03)):
            setting = patch.object(bleak_nus, name, value)
            setting.start()
            self.addCleanup(setting.stop)
        quarantine = patch.dict(bleak_nus._QUARANTINED_ADDRESSES, clear=True)
        quarantine.start()
        self.addCleanup(quarantine.stop)

    def transport(self):
        transport = bleak_nus.BleakNusTransport(pairer=None, address="AA:BB:CC:DD:EE:FF")
        client = Mock(is_connected=True)
        client.stop_notify = AsyncMock()
        client.disconnect = AsyncMock()
        transport._client = client
        transport._tx = object()
        return transport, client

    async def test_hung_steps_finish_with_failure_despite_repeated_cancellation(self):
        for phase in ("stop_notify", "disconnect", "both"):
            with self.subTest(phase=phase):
                bleak_nus._QUARANTINED_ADDRESSES.clear()
                transport, client = self.transport()
                entered, release = asyncio.Event(), asyncio.Event()

                async def hung(*args):
                    entered.set()
                    while not release.is_set():
                        try:
                            await release.wait()
                        except asyncio.CancelledError:
                            pass

                if phase in ("stop_notify", "both"):
                    client.stop_notify.side_effect = hung
                if phase in ("disconnect", "both"):
                    client.disconnect.side_effect = hung

                async def close_with_outer_timeout():
                    async with asyncio.timeout(0.005):
                        await transport.close()

                task = asyncio.create_task(close_with_outer_timeout())
                try:
                    await entered.wait()
                    await asyncio.sleep(0.01)
                    task.cancel()
                    done, _ = await asyncio.wait({task}, timeout=0.3)
                    self.assertIn(task, done)
                    with self.assertRaises(TransportCleanupError):
                        task.result()
                    client.disconnect.assert_awaited_once()
                    with self.assertRaises(TransportCleanupError):
                        await transport.connect()
                    replacement = bleak_nus.BleakNusTransport(pairer=None, address=transport.address)
                    with self.assertRaises(TransportCleanupError):
                        await replacement.connect()
                finally:
                    release.set()
                    await asyncio.gather(*transport._cleanup_operations, return_exceptions=True)
                    await asyncio.gather(task, return_exceptions=True)

    async def test_repair_fails_without_creating_second_client(self):
        transport, client = self.transport()
        entered = asyncio.Event()

        async def hung(*args):
            entered.set()
            await asyncio.Event().wait()

        client.stop_notify.side_effect = hung
        transport.connect = AsyncMock(side_effect=ConnectionError("failed"))
        factory = Mock(return_value=transport)
        manager = DeviceManager(factory)
        session = DeviceSession("Panel", manager)
        runner = asyncio.create_task(session.run())
        await entered.wait()
        repair = asyncio.create_task(session.run_exclusive(
            lambda transport: transport.reset(), interrupt_connect=True,
        ))
        done, _ = await asyncio.wait({repair, runner}, timeout=0.3)
        self.assertEqual(done, {repair, runner})
        for task in (repair, runner):
            with self.assertRaises(TransportCleanupError):
                task.result()
        self.assertIsNotNone(manager.cleanup_error)
        with self.assertRaises(TransportCleanupError):
            await manager.connect()
        factory.assert_called_once()
        client.disconnect.assert_awaited_once()

    async def test_concurrent_close_shares_cleanup_and_propagates_cancellation(self):
        transport, client = self.transport()
        entered, release = asyncio.Event(), asyncio.Event()

        async def blocked(*args):
            entered.set()
            await release.wait()

        client.stop_notify.side_effect = blocked
        first = asyncio.create_task(transport.close())
        await entered.wait()
        second = asyncio.create_task(transport.close())
        first.cancel()
        release.set()
        results = await asyncio.gather(first, second, return_exceptions=True)
        self.assertIsInstance(results[0], asyncio.CancelledError)
        self.assertIsNone(results[1])
        client.disconnect.assert_awaited_once()

    def test_cleanup_failure_is_visible_even_before_first_connection(self):
        device = DeviceStatus("Panel", False, False, cleanup_failed=True)
        status = PanelStatus((device,), {}, 0, None)
        self.assertEqual(status.state, TrayState.WARN)
        self.assertIn("Bridge", format_device_summary(status.devices))


if __name__ == "__main__":
    unittest.main()
