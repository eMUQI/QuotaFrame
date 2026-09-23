from __future__ import annotations

import asyncio
import unittest
from unittest.mock import AsyncMock, Mock

from quotaframe_bridge.protocol.messages import IncompatibleProtocolError

from quotaframe_bridge.service.device_manager import DeviceManager, ReconnectBackoff
from quotaframe_bridge.transports.base import (
    TransportConnectionError,
    TransportReset,
    WritePolicy,
)


class ReconnectBackoffTests(unittest.TestCase):
    def test_backoff_caps_and_resets(self) -> None:
        backoff = ReconnectBackoff(initial=2, maximum=60)

        self.assertEqual(
            [backoff.next_delay() for _ in range(7)],
            [2, 4, 8, 16, 32, 60, 60],
        )
        backoff.reset()
        self.assertEqual(backoff.next_delay(), 2)

    def test_invalid_bounds_are_rejected(self) -> None:
        with self.assertRaises(ValueError):
            ReconnectBackoff(initial=0, maximum=60)
        with self.assertRaises(ValueError):
            ReconnectBackoff(initial=10, maximum=2)


class FakeTransport:
    def __init__(self, *, connect_error: bool = False, send_error: bool = False) -> None:
        self.connect_error = connect_error
        self.send_error = send_error
        self.connected = False
        self.sent = 0
        self.policies: list[WritePolicy] = []
        self.status = object()

    async def connect(self) -> None:
        if self.connect_error:
            raise RuntimeError("external BLE error")
        self.connected = True

    async def send_command(
        self, payload: bytes, command: str, expected_ack_n: int,
        write_policy: WritePolicy = WritePolicy.NORMAL,
    ) -> None:
        self.sent += 1
        self.policies.append(write_policy)
        if self.send_error:
            raise RuntimeError("link lost")

    async def close(self) -> None:
        self.connected = False

    async def query_status(self) -> object:
        return self.status


class PublicErrorTransport(FakeTransport):
    async def connect(self) -> None:
        raise TransportConnectionError(
            "retry once with --repair-pairing"
        )


class DeviceManagerTests(unittest.IsolatedAsyncioTestCase):
    async def test_negotiation_preserves_close_and_reset_wakeups(self) -> None:
        for action in ("close", "reset"):
            with self.subTest(action=action):
                negotiating = asyncio.Event()
                release = asyncio.Event()

                async def reject() -> None:
                    negotiating.set()
                    await release.wait()
                    raise IncompatibleProtocolError("unsupported")

                rejected = FakeTransport()
                rejected.connect = AsyncMock(side_effect=reject)
                rejected.close = AsyncMock()
                working = FakeTransport()
                factory = Mock(side_effect=[rejected, working])
                manager = DeviceManager(factory)
                task = asyncio.create_task(manager.connect())
                try:
                    await asyncio.wait_for(negotiating.wait(), 1)
                    await getattr(manager, action)()
                    release.set()
                    if action == "close":
                        with self.assertRaises(asyncio.CancelledError):
                            await asyncio.wait_for(task, 1)
                        self.assertFalse(manager.connected)
                        self.assertEqual(factory.call_count, 1)
                    else:
                        await asyncio.wait_for(task, 1)
                        self.assertTrue(manager.connected)
                        self.assertFalse(manager.incompatible)
                        self.assertEqual(factory.call_count, 2)
                    rejected.close.assert_awaited_once()
                finally:
                    task.cancel()
                    await asyncio.gather(task, return_exceptions=True)
                    await manager.close()

    async def test_close_releases_incompatible_connection_wait(self) -> None:
        transport = FakeTransport()
        transport.connect = AsyncMock(side_effect=IncompatibleProtocolError("unsupported"))
        manager = DeviceManager(lambda: transport)
        task = asyncio.create_task(manager.connect())
        try:
            for _ in range(10):
                await asyncio.sleep(0)
            self.assertTrue(manager.incompatible)
            await manager.close()
            with self.assertRaises(asyncio.CancelledError):
                await asyncio.wait_for(task, 1)
            transport.connect.assert_awaited_once()
        finally:
            task.cancel()
            await asyncio.gather(task, return_exceptions=True)

    async def test_incompatible_device_waits_for_reset_without_retrying(self) -> None:
        rejected = FakeTransport()
        rejected.connect = AsyncMock(side_effect=IncompatibleProtocolError("unsupported"))
        rejected.close = AsyncMock()
        working = FakeTransport()
        transports = [rejected, working]
        sleep = AsyncMock()
        manager = DeviceManager(lambda: transports.pop(0), sleep=sleep)
        task = asyncio.create_task(manager.connect())
        try:
            for _ in range(10):
                await asyncio.sleep(0)
            self.assertTrue(manager.incompatible)
            self.assertFalse(manager.connected)
            self.assertEqual(len(transports), 1)
            rejected.close.assert_awaited_once()
            sleep.assert_not_awaited()
            await manager.reset()
            await asyncio.wait_for(task, 1)
            self.assertTrue(manager.connected)
            self.assertFalse(manager.incompatible)
        finally:
            await manager.close()
            task.cancel()
            await asyncio.gather(task, return_exceptions=True)

    async def test_forwards_folder_push_write_policy_and_status_query(self) -> None:
        transport = FakeTransport()
        manager = DeviceManager(lambda: transport)
        await manager.connect()

        await manager.send_command(
            b"folder push\n", "chunk", 2880, WritePolicy.FOLDER_PUSH
        )

        self.assertEqual(transport.policies, [WritePolicy.FOLDER_PUSH])
        self.assertIs(await manager.query_status(), transport.status)

    async def test_default_retry_delay_stays_within_five_seconds(self) -> None:
        transports = [FakeTransport(connect_error=True) for _ in range(6)]
        transports.append(FakeTransport())
        delays: list[float] = []
        manager = DeviceManager(
            lambda: transports.pop(0),
            sleep=lambda delay: self._record_delay(delays, delay),
        )
        await manager.connect()
        self.assertEqual(delays, [2, 4, 5, 5, 5, 5])
        await manager.close()

    async def test_connect_retries_with_backoff(self) -> None:
        transports = [
            FakeTransport(connect_error=True),
            FakeTransport(connect_error=False),
        ]
        delays: list[float] = []
        manager = DeviceManager(
            transport_factory=lambda: transports.pop(0),
            backoff=ReconnectBackoff(initial=2, maximum=60),
            sleep=lambda delay: self._record_delay(delays, delay),
        )

        await manager.connect()

        self.assertEqual(delays, [2])

    async def test_send_failure_reconnects_and_requires_new_sequence(self) -> None:
        first = FakeTransport(send_error=True)
        second = FakeTransport()
        transports = [first, second]
        manager = DeviceManager(
            transport_factory=lambda: transports.pop(0),
            sleep=lambda _: self._no_delay(),
        )
        await manager.connect()

        with self.assertRaises(TransportReset):
            await manager.send_command(b"first\n", "usage", 7)
        await manager.send_command(b"second\n", "usage", 8)

        self.assertEqual(first.sent, 1)
        self.assertEqual(second.sent, 1)

    async def test_sanitized_transport_guidance_is_logged(self) -> None:
        transports = [PublicErrorTransport(), FakeTransport()]
        manager = DeviceManager(
            transport_factory=lambda: transports.pop(0),
            sleep=lambda _: self._no_delay(),
        )

        with self.assertLogs(
            "quotaframe_bridge.service.device_manager",
            level="WARNING",
        ) as captured:
            await manager.connect()

        self.assertIn("--repair-pairing", "\n".join(captured.output))

    async def _record_delay(self, target: list[float], delay: float) -> None:
        target.append(delay)

    async def _no_delay(self) -> None:
        return None


class ConnectionStateTests(unittest.IsolatedAsyncioTestCase):
    async def test_starts_disconnected_and_never_connected(self) -> None:
        manager = DeviceManager(lambda: FakeTransport())
        self.assertFalse(manager.connected)
        self.assertFalse(manager.ever_connected)

    async def test_connect_sets_both_flags(self) -> None:
        manager = DeviceManager(lambda: FakeTransport())
        await manager.connect()
        self.assertTrue(manager.connected)
        self.assertTrue(manager.ever_connected)

    async def test_reset_clears_connected_but_keeps_ever_connected(self) -> None:
        manager = DeviceManager(lambda: FakeTransport())
        await manager.connect()
        await manager.reset()
        self.assertFalse(manager.connected)
        self.assertTrue(manager.ever_connected)

    async def test_send_after_reset_reconnects(self) -> None:
        built: list[FakeTransport] = []

        def factory() -> FakeTransport:
            transport = FakeTransport()
            built.append(transport)
            return transport

        manager = DeviceManager(factory)
        await manager.connect()
        await manager.reset()
        await manager.send_command(b"payload", "usage", 1)
        self.assertTrue(manager.connected)
        self.assertEqual(len(built), 2)

    async def test_close_leaves_ever_connected_true(self) -> None:
        manager = DeviceManager(lambda: FakeTransport())
        await manager.connect()
        await manager.close()
        self.assertFalse(manager.connected)
        self.assertTrue(manager.ever_connected)


if __name__ == "__main__":
    unittest.main()
