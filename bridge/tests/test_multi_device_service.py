from __future__ import annotations

import asyncio
import json
import unittest
from datetime import datetime, timezone
from unittest.mock import patch

from quotaframe_bridge.domain.models import (
    Provider,
    ProviderUsage,
    SourceState,
    UsageWindow,
)
from quotaframe_bridge.protocol.messages import DeviceStatus
from quotaframe_bridge.service import multi_device as multi_device_module
from quotaframe_bridge.service.multi_device import (
    DeviceSession,
    MultiDeviceBridgeService,
    UsageSnapshot,
)
from quotaframe_bridge.sources.codexbar import CodexBarResolutionError
from quotaframe_bridge.transports.base import TransportReset


class RecordingTransport:
    def __init__(
        self,
        connect_gate: asyncio.Event | None = None,
        *,
        capabilities: frozenset[str] = frozenset(),
        time_sync_gate: asyncio.Event | None = None,
    ) -> None:
        self.connect_gate = connect_gate
        self.connected = False
        self.closed = False
        self.sent: list[tuple[bytes, str, int]] = []
        self.time_sync_gate = time_sync_gate
        self.device_status = DeviceStatus(
            name="Panel",
            secure=True,
            protocol=1,
            page="overview",
            capabilities=capabilities,
        )

    async def connect(self) -> None:
        if self.connect_gate is not None:
            await self.connect_gate.wait()
        self.connected = True

    async def send_command(
        self,
        payload: bytes,
        command: str,
        sequence: int,
    ) -> None:
        if command == "time_sync" and self.time_sync_gate is not None:
            await self.time_sync_gate.wait()
        self.sent.append((payload, command, sequence))

    async def close(self) -> None:
        self.closed = True
        self.connected = False


class ResettingTransport(RecordingTransport):
    def __init__(self, failures: int) -> None:
        super().__init__()
        self.failures = failures

    async def send_command(
        self,
        payload: bytes,
        command: str,
        sequence: int,
    ) -> None:
        if self.failures > 0:
            self.failures -= 1
            raise TransportReset("reconnected")
        await super().send_command(payload, command, sequence)


class UnexpectedFailureTransport(RecordingTransport):
    def __init__(self) -> None:
        super().__init__()
        self.connect_count = 0
        self.failures = 1
        self.recovered = asyncio.Event()

    async def connect(self) -> None:
        self.connect_count += 1
        self.closed = False
        await super().connect()

    async def send_command(
        self,
        payload: bytes,
        command: str,
        sequence: int,
    ) -> None:
        if self.failures > 0:
            self.failures -= 1
            raise RuntimeError("unexpected session failure")
        await super().send_command(payload, command, sequence)
        self.recovered.set()


def snapshot(sampled_at: int) -> UsageSnapshot:
    return UsageSnapshot(
        codex=ProviderUsage(
            Provider.CODEX,
            SourceState.UNAVAILABLE,
            sampled_at,
        ),
        claude=ProviderUsage(
            Provider.CLAUDE,
            SourceState.UNAVAILABLE,
            sampled_at,
        ),
    )


class RecordingSource:
    def __init__(self, *, fail: bool = False) -> None:
        self.fail = fail
        self.calls: list[int] = []

    async def collect(
        self,
        *,
        attempted_at: int,
    ) -> dict[Provider, ProviderUsage]:
        self.calls.append(attempted_at)
        if self.fail:
            raise RuntimeError("private source failure")
        return {
            Provider.CODEX: ProviderUsage(
                Provider.CODEX,
                SourceState.UNAVAILABLE,
                attempted_at,
            ),
            Provider.CLAUDE: ProviderUsage(
                Provider.CLAUDE,
                SourceState.UNAVAILABLE,
                attempted_at,
            ),
        }


class DeviceSessionTests(unittest.IsolatedAsyncioTestCase):
    async def test_clock_rollback_preserves_cache_and_publishes_both_providers(self):
        from quotaframe_bridge.service.state import UsageStateStore

        cached = ProviderUsage(Provider.CODEX, SourceState.PARTIAL, 1000,
                               short=UsageWindow(42))
        other = ProviderUsage(Provider.CLAUDE, SourceState.PARTIAL, 600,
                              short=UsageWindow(34))
        store = UsageStateStore()
        store.merge_collection({Provider.CODEX: cached, Provider.CLAUDE: other},
                               attempted_at=1000)
        store.mark_all_unavailable(attempted_at=699)
        update = UsageSnapshot(store.providers[Provider.CODEX],
                               store.providers[Provider.CLAUDE])
        transport = RecordingTransport()
        now = 700
        session = DeviceSession("panel", transport,
            wall_now=lambda: datetime.fromtimestamp(now, timezone.utc))
        try:
            for now, expected in ((700, "partial"), (699, "unavailable"),
                                  (698, "unavailable"), (1001, "partial")):
                await session._publish(update)
                messages = [json.loads(data) for data, _, _ in transport.sent[-2:]]
                self.assertEqual(messages[0]["state"], expected)
                self.assertEqual(messages[0]["sampled_at"],
                                 str(now) if expected == "unavailable" else "1000")
                self.assertEqual(messages[1]["state"], "partial")
                self.assertEqual(messages[1]["short_used_pct"], "34")
            self.assertEqual(store.providers[Provider.CODEX], cached)
            self.assertEqual(store.last_valid[Provider.CODEX], cached)
            self.assertEqual(update.codex, cached)
        finally:
            await session.close()

    async def test_repair_interrupts_connection_retry_and_consumes_repair_flag(self):
        from unittest.mock import AsyncMock
        from quotaframe_bridge.service.device_manager import DeviceManager
        from quotaframe_bridge.protocol.messages import IncompatibleProtocolError
        for failure in (ConnectionError("stale bond"), IncompatibleProtocolError("unsupported")):
            for reconnect in (False, True):
                failed = asyncio.Event()
                ready = asyncio.Event()
                repaired = False
                consumed = False
                old = AsyncMock()
                async def fail():
                    failed.set()
                    raise failure
                old.connect.side_effect = fail
                new = AsyncMock()
                async def succeed():
                    nonlocal consumed
                    consumed = repaired
                    ready.set()
                new.connect.side_effect = succeed
                manager = DeviceManager(lambda: new if repaired else old)
                session = DeviceSession("panel", manager)
                async def repair(transport):
                    nonlocal repaired
                    old.close.assert_awaited()
                    repaired = True
                    await transport.reset()
                if reconnect:
                    manager._transport = new
                    runner = asyncio.create_task(session.run())
                    await asyncio.sleep(0)
                    await asyncio.sleep(0)
                    manager._transport = None
                    session.offer(snapshot(100))
                else:
                    runner = asyncio.create_task(session.run())
                try:
                    await asyncio.wait_for(failed.wait(), 1)
                    await asyncio.wait_for(session.run_exclusive(repair, interrupt_connect=True), 1)
                    await asyncio.wait_for(ready.wait(), 1)
                    self.assertTrue(consumed)
                    self.assertFalse(session.exclusive_active)
                finally:
                    await session.close()
                    await asyncio.gather(runner, return_exceptions=True)

    async def test_repair_waits_for_ble_cleanup_before_reset(self):
        from unittest.mock import AsyncMock, Mock
        from quotaframe_bridge.service.device_manager import DeviceManager
        from quotaframe_bridge.transports.bleak_nus import BleakNusTransport
        from quotaframe_bridge.protocol.messages import IncompatibleProtocolError
        for failure in (ConnectionError("failed"), IncompatibleProtocolError("unsupported")):
            for phase in ("stop_notify", "disconnect"):
                closing = asyncio.Event()
                release = asyncio.Event()
                repaired = asyncio.Event()
                old = BleakNusTransport(pairer=None)
                old._client = client = Mock(is_connected=True)
                old._tx = object()
                client.stop_notify = AsyncMock()
                client.disconnect = AsyncMock()
                async def blocked(*args):
                    closing.set()
                    await release.wait()
                getattr(client, phase).side_effect = blocked
                old.connect = AsyncMock(side_effect=failure)
                manager = DeviceManager(lambda: old)
                session = DeviceSession("panel", manager)
                async def repair(transport):
                    client.disconnect.assert_awaited_once()
                    self.assertTrue(release.is_set())
                    repaired.set()
                    await transport.reset()
                runner = asyncio.create_task(session.run())
                operation = None
                try:
                    await asyncio.wait_for(closing.wait(), 1)
                    operation = asyncio.create_task(session.run_exclusive(repair, interrupt_connect=True))
                    await asyncio.sleep(0)
                    await asyncio.sleep(0)
                    self.assertFalse(repaired.is_set())
                    release.set()
                    await asyncio.wait_for(operation, 1)
                    self.assertTrue(repaired.is_set())
                finally:
                    release.set()
                    if operation is not None:
                        await asyncio.gather(operation, return_exceptions=True)
                    await session.close()
                    await asyncio.gather(runner, return_exceptions=True)

    async def test_repair_does_not_interrupt_an_exclusive_operation(self):
        from unittest.mock import AsyncMock
        session = DeviceSession("panel", RecordingTransport())
        entered = asyncio.Event()
        async def upgrade(transport):
            entered.set()
            await asyncio.Event().wait()
        task = asyncio.create_task(session.run_exclusive(upgrade))
        try:
            await entered.wait()
            repair = AsyncMock()
            with self.assertRaises(RuntimeError):
                await session.run_exclusive(repair, interrupt_connect=True)
            repair.assert_not_awaited()
            self.assertFalse(task.done())
        finally:
            task.cancel()
            await asyncio.gather(task, return_exceptions=True)

    async def test_queued_publication_reads_latest_snapshot_and_actual_send_time(self):
        transport = RecordingTransport()
        session = DeviceSession("panel", transport,
                                wall_now=lambda: datetime.fromtimestamp(300, timezone.utc))
        old = snapshot(100)
        session.offer(old)
        await session._exclusive_lock.acquire()
        task = asyncio.create_task(session._publish(old))
        await asyncio.sleep(0)
        session.offer(snapshot(200))
        session._exclusive_lock.release()
        await asyncio.wait_for(task, .5)
        for payload, command, seq in transport.sent:
            message = json.loads(payload)
            self.assertEqual(message["sampled_at"], "200")
            self.assertEqual(message["sent_at"], "300")

    async def test_page_turn_targets_all_capable_devices_and_respects_busy(self):
        transports = [RecordingTransport(capabilities=frozenset(caps)) for caps in (
            {"screen.page.v1"}, {"screen.page.v1"}, {"screen.toggle.v1"}, {"screen.page.v1"})]
        sessions = tuple(DeviceSession(str(i), t) for i, t in enumerate(transports))
        for session in sessions:
            session._connected = True
            session.transport.connected = True
        sessions[3]._exclusive_active = True
        service = MultiDeviceBridgeService(RecordingSource(), sessions)
        service.turn_pages(-1)
        await asyncio.gather(*(s._screen_command_task for s in sessions))
        for transport in transports[:2]:
            self.assertEqual(json.loads(transport.sent[0][0]),
                {"cmd": "screen_page", "v": "1", "seq": "0", "previous": "1"})
        self.assertEqual(transports[2].sent, [])
        self.assertEqual(transports[3].sent, [])
        sessions[0].schedule_screen_page(1)
        await sessions[0]._screen_command_task
        self.assertEqual(json.loads(transports[0].sent[1][0])["previous"], "0")
        with self.assertRaises(ValueError):
            sessions[0].schedule_screen_page(0)

    async def test_close_cancels_a_screen_toggle_waiting_for_transport(self) -> None:
        started = asyncio.Event()
        cancelled = asyncio.Event()

        class BlockedToggleTransport(RecordingTransport):
            async def send_command(self, payload, command, sequence) -> None:
                started.set()
                try:
                    await asyncio.Event().wait()
                finally:
                    cancelled.set()

        transport = BlockedToggleTransport(
            capabilities=frozenset({"screen.toggle.v1"})
        )
        session = DeviceSession("WS", transport)
        session._connected = True
        session.transport.connected = True
        session.schedule_screen_toggle()
        await started.wait()
        await session.close()
        self.assertTrue(cancelled.is_set())
        self.assertFalse(session.connected)

    async def test_exclusive_operation_drops_a_queued_screen_toggle(self) -> None:
        transport = RecordingTransport(capabilities=frozenset({"screen.toggle.v1"}))
        session = DeviceSession("WS", transport)
        session._connected = True
        session.transport.connected = True
        await session._exclusive_lock.acquire()
        session.schedule_screen_toggle()
        queued = session._screen_command_task
        await asyncio.sleep(0)
        session._exclusive_lock.release()

        async def operation(_):
            return "done"

        self.assertEqual(await session.run_exclusive(operation), "done")
        self.assertTrue(queued.done())
        self.assertEqual(transport.sent, [])

    async def test_screen_toggle_only_targets_connected_capable_sessions(self) -> None:
        transports = [RecordingTransport(capabilities=frozenset(caps)) for caps in (
            {"screen.toggle.v1"}, set(), {"screen.toggle.v1"}, {"screen.toggle.v1"},
        )]
        sessions = tuple(DeviceSession(str(i), t) for i, t in enumerate(transports))
        for session in (sessions[0], sessions[1], sessions[3]):
            session._connected = True
            session.transport.connected = True
        sessions[3]._exclusive_active = True
        service = MultiDeviceBridgeService(RecordingSource(), sessions)
        service.toggle_screensavers()
        await asyncio.gather(*(s._screen_command_task for s in sessions))
        self.assertEqual(json.loads(transports[0].sent[0][0]),
                         {"cmd": "screen_toggle", "v": "1", "seq": "0"})
        self.assertEqual([len(t.sent) for t in transports], [1, 0, 0, 0])
        service.toggle_screensavers()
        await sessions[0]._screen_command_task
        self.assertEqual(transports[0].sent[1][2], 1)

    async def test_second_click_is_dropped_while_one_is_in_flight(self) -> None:
        transport = RecordingTransport(capabilities=frozenset({"screen.toggle.v1"}))
        session = DeviceSession("WS", transport)
        session._connected = True
        session.transport.connected = True
        await session._exclusive_lock.acquire()
        session.schedule_screen_toggle()
        await asyncio.sleep(0)
        session.schedule_screen_toggle()
        session._exclusive_lock.release()
        await session._screen_command_task
        self.assertEqual(len(transport.sent), 1)

    async def test_screen_toggle_waits_for_usage_and_does_not_replay_after_reset(self) -> None:
        transport = ResettingTransport(failures=1)
        transport.device_status = RecordingTransport(
            capabilities=frozenset({"screen.toggle.v1"})).device_status
        session = DeviceSession("WS", transport)
        session._connected = True
        session.transport.connected = True
        await session._exclusive_lock.acquire()
        session.schedule_screen_toggle()
        await asyncio.sleep(0)
        self.assertFalse(session._screen_command_task.done())
        session._exclusive_lock.release()
        await session._screen_command_task
        self.assertEqual(transport.failures, 0)
        self.assertEqual(transport.sent, [])

    async def test_capable_session_syncs_on_connect_and_after_six_hours(self) -> None:
        monotonic_value = 0.0

        def monotonic() -> float:
            return monotonic_value

        transport = RecordingTransport(
            capabilities=frozenset({"usage.v1", "time.sync.v1"})
        )
        session = DeviceSession(
            "Waveshare",
            transport,
            wall_now=lambda: datetime(2028, 2, 29, tzinfo=timezone.utc),
            monotonic=monotonic,
        )
        task = asyncio.create_task(session.run())
        try:
            first_revision = session.offer(snapshot(100))
            await asyncio.wait_for(
                session.wait_delivered(first_revision),
                timeout=0.1,
            )
            await asyncio.sleep(0)
            self.assertEqual(
                [command for _, command, _ in transport.sent],
                ["usage", "usage", "time_sync"],
            )

            monotonic_value = 21_600.0
            second_revision = session.offer(snapshot(200))
            await asyncio.wait_for(
                session.wait_delivered(second_revision),
                timeout=0.1,
            )
            await asyncio.sleep(0)
            self.assertEqual(
                [command for _, command, _ in transport.sent],
                [
                    "usage",
                    "usage",
                    "time_sync",
                    "usage",
                    "usage",
                    "time_sync",
                ],
            )
            self.assertEqual(
                [sequence for _, _, sequence in transport.sent],
                [0, 1, 2, 3, 4, 5],
            )
        finally:
            await session.close()
            await asyncio.gather(task, return_exceptions=True)

    async def test_blocked_time_sync_does_not_delay_snapshot_delivery(self) -> None:
        gate = asyncio.Event()
        transport = RecordingTransport(
            capabilities=frozenset({"usage.v1", "time.sync.v1"}),
            time_sync_gate=gate,
        )
        session = DeviceSession("Waveshare", transport)
        task = asyncio.create_task(session.run())
        try:
            revision = session.offer(snapshot(100))
            await asyncio.wait_for(session.wait_delivered(revision), timeout=0.1)
            self.assertEqual(
                [command for _, command, _ in transport.sent],
                ["usage", "usage"],
            )
        finally:
            gate.set()
            await session.close()
            await asyncio.gather(task, return_exceptions=True)

    async def test_usage_and_exclusive_wait_for_inflight_time_sync(self) -> None:
        gate = asyncio.Event()
        transport = RecordingTransport(
            capabilities=frozenset({"usage.v1", "time.sync.v1"}),
            time_sync_gate=gate,
        )
        session = DeviceSession("Waveshare", transport)
        task = asyncio.create_task(session.run())
        try:
            first = session.offer(snapshot(100))
            await asyncio.wait_for(session.wait_delivered(first), timeout=0.1)
            await asyncio.sleep(0)

            second = session.offer(snapshot(200))
            await asyncio.sleep(0)
            self.assertEqual(len(transport.sent), 2)
            gate.set()
            await asyncio.wait_for(session.wait_delivered(second), timeout=1)
            self.assertEqual(
                [command for _, command, _ in transport.sent],
                ["usage", "usage", "time_sync", "usage", "usage"],
            )

            ran = False

            async def exclusive(_transport):
                nonlocal ran
                ran = True

            await asyncio.wait_for(session.run_exclusive(exclusive), timeout=0.1)
            self.assertTrue(ran)
        finally:
            gate.set()
            await session.close()
            await asyncio.gather(task, return_exceptions=True)

    async def test_usage_reset_forces_time_sync_before_six_hours(self) -> None:
        monotonic_value = 0.0

        def monotonic() -> float:
            return monotonic_value

        transport = RecordingTransport(
            capabilities=frozenset({"usage.v1", "time.sync.v1"})
        )
        session = DeviceSession("Waveshare", transport, monotonic=monotonic)
        task = asyncio.create_task(session.run())
        try:
            first = session.offer(snapshot(100))
            await asyncio.wait_for(session.wait_delivered(first), timeout=0.1)
            await asyncio.sleep(0)
            self.assertEqual(
                [command for _, command, _ in transport.sent].count("time_sync"),
                1,
            )

            original_send = transport.send_command
            reset_once = True

            async def reset_then_send(payload, command, sequence):
                nonlocal reset_once
                if command == "usage" and reset_once:
                    reset_once = False
                    raise TransportReset("reconnected")
                await original_send(payload, command, sequence)

            transport.send_command = reset_then_send
            monotonic_value = 60.0
            second = session.offer(snapshot(200))
            await asyncio.wait_for(session.wait_delivered(second), timeout=0.1)
            await asyncio.sleep(0)
            self.assertEqual(
                [command for _, command, _ in transport.sent].count("time_sync"),
                2,
            )
        finally:
            await session.close()
            await asyncio.gather(task, return_exceptions=True)

    async def test_exclusive_operation_blocks_usage_until_it_finishes(self) -> None:
        transport = RecordingTransport()
        session = DeviceSession("M5", transport)
        entered = asyncio.Event()
        release = asyncio.Event()

        async def exclusive(_transport):
            entered.set()
            await release.wait()

        operation = asyncio.create_task(session.run_exclusive(exclusive))
        await entered.wait()
        update = snapshot(10)
        publish = asyncio.create_task(session._publish(update))
        await asyncio.sleep(0)

        self.assertTrue(session.exclusive_active)
        self.assertEqual(transport.sent, [])
        release.set()
        await operation
        await publish
        self.assertFalse(session.exclusive_active)
        self.assertEqual(len(transport.sent), 2)

    async def test_blocked_session_does_not_stop_connected_session(self) -> None:
        blocked_gate = asyncio.Event()
        m5_transport = RecordingTransport()
        ws_transport = RecordingTransport(blocked_gate)
        m5 = DeviceSession("M5", m5_transport)
        ws = DeviceSession("Waveshare", ws_transport)
        m5_task = asyncio.create_task(m5.run())
        ws_task = asyncio.create_task(ws.run())
        try:
            update = snapshot(100)
            revision = m5.offer(update)
            ws.offer(update)
            await asyncio.wait_for(m5.wait_delivered(revision), timeout=0.1)

            self.assertEqual(len(m5_transport.sent), 2)
            self.assertEqual(ws_transport.sent, [])
        finally:
            await m5.close()
            await ws.close()
            await asyncio.gather(m5_task, ws_task, return_exceptions=True)

    async def test_reconnecting_session_publishes_only_latest_snapshot(
        self,
    ) -> None:
        gate = asyncio.Event()
        transport = RecordingTransport(gate)
        session = DeviceSession("Waveshare", transport)
        task = asyncio.create_task(session.run())
        try:
            session.offer(snapshot(100))
            latest_revision = session.offer(snapshot(200))
            gate.set()
            await asyncio.wait_for(
                session.wait_delivered(latest_revision),
                timeout=0.1,
            )

            payloads = [json.loads(payload) for payload, _, _ in transport.sent]
            self.assertEqual(
                {item["sampled_at"] for item in payloads},
                {"200"},
            )
            self.assertEqual(len(payloads), 2)
        finally:
            await session.close()
            await asyncio.gather(task, return_exceptions=True)

    async def test_each_session_owns_an_independent_sequence(self) -> None:
        first_transport = RecordingTransport()
        second_transport = RecordingTransport()
        first = DeviceSession("M5", first_transport)
        second = DeviceSession("Waveshare", second_transport)
        first_task = asyncio.create_task(first.run())
        second_task = asyncio.create_task(second.run())
        try:
            update = snapshot(100)
            first_revision = first.offer(update)
            second_revision = second.offer(update)
            await asyncio.wait_for(
                asyncio.gather(
                    first.wait_delivered(first_revision),
                    second.wait_delivered(second_revision),
                ),
                timeout=0.1,
            )

            self.assertEqual(
                [sequence for _, _, sequence in first_transport.sent],
                [0, 1],
            )
            self.assertEqual(
                [sequence for _, _, sequence in second_transport.sent],
                [0, 1],
            )
        finally:
            await first.close()
            await second.close()
            await asyncio.gather(
                first_task,
                second_task,
                return_exceptions=True,
            )

    async def test_close_cancels_a_blocked_connection(self) -> None:
        gate = asyncio.Event()
        transport = RecordingTransport(gate)
        session = DeviceSession("Waveshare", transport)
        task = asyncio.create_task(session.run())
        await asyncio.sleep(0)

        await asyncio.wait_for(session.close(), timeout=0.1)
        await asyncio.gather(task, return_exceptions=True)

        self.assertTrue(transport.closed)

    async def test_session_survives_repeated_transport_resets(self) -> None:
        transport = ResettingTransport(failures=2)
        session = DeviceSession("Waveshare", transport)
        task = asyncio.create_task(session.run())
        try:
            revision = session.offer(snapshot(100))

            await asyncio.wait_for(
                session.wait_delivered(revision),
                timeout=0.1,
            )

            self.assertEqual(len(transport.sent), 2)
            self.assertFalse(task.done())
        finally:
            await session.close()
            await asyncio.gather(task, return_exceptions=True)


class MultiDeviceBridgeServiceTests(unittest.IsolatedAsyncioTestCase):
    async def test_added_session_receives_cached_usage_without_waiting_for_tick(self):
        source = RecordingSource()
        service = MultiDeviceBridgeService(
            source, (), interval=3600, publish_interval=3600,
        )
        runner = asyncio.create_task(service.run())
        transport = RecordingTransport()
        session = DeviceSession("new panel", transport)
        try:
            async with asyncio.timeout(1):
                while service.collection_count == 0:
                    await asyncio.sleep(0)
            service.add_session(session)
            await asyncio.wait_for(session.wait_delivered(1), timeout=1)
            messages = [json.loads(data) for data, command, _ in transport.sent
                        if command == "usage"]
            self.assertEqual([message["provider"] for message in messages],
                             ["codex", "claude"])
            self.assertEqual(len(source.calls), 1)
        finally:
            runner.cancel()
            await asyncio.gather(runner, return_exceptions=True)

    async def test_unexpected_session_failure_is_logged_and_restarted(self) -> None:
        transport = UnexpectedFailureTransport()
        service = MultiDeviceBridgeService(
            RecordingSource(),
            (DeviceSession("M5", transport),),
            interval=3600.0,
            publish_interval=3600.0,
        )

        with patch.object(
            multi_device_module,
            "_SESSION_RESTART_INITIAL_DELAY",
            0.001,
        ):
            with self.assertLogs(multi_device_module.LOGGER, level="ERROR") as logs:
                runner = asyncio.create_task(service.run())
                try:
                    await asyncio.wait_for(transport.recovered.wait(), timeout=0.2)
                finally:
                    runner.cancel()
                    await asyncio.gather(runner, return_exceptions=True)

        self.assertEqual(transport.connect_count, 2)
        self.assertTrue(
            any(
                "M5 session stopped unexpectedly; restarting" in message
                for message in logs.output
            )
        )

    async def test_resolution_failure_records_category_and_consecutive_count(
        self,
    ) -> None:
        source = RecordingSource()
        source.fail = False

        async def fail_resolution(*, attempted_at: int):
            raise CodexBarResolutionError("not_found")

        source.collect = fail_resolution  # type: ignore[method-assign]
        service = MultiDeviceBridgeService(
            source,
            (DeviceSession("M5", RecordingTransport()),),
            clock=lambda: 100,
        )

        await service.collect()

        self.assertEqual(service.consecutive_failures, 1)
        self.assertEqual(service.resolution_error, "not_found")

    async def test_collects_once_and_offers_same_snapshot_to_both_sessions(
        self,
    ) -> None:
        source = RecordingSource()
        first_transport = RecordingTransport()
        second_transport = RecordingTransport()
        service = MultiDeviceBridgeService(
            source,
            (
                DeviceSession("M5", first_transport),
                DeviceSession("Waveshare", second_transport),
            ),
            interval=0,
            clock=lambda: 100,
        )

        await service.run(cycles=1)

        self.assertEqual(source.calls, [100])
        self.assertEqual(
            [json.loads(payload) for payload, _, _ in first_transport.sent],
            [json.loads(payload) for payload, _, _ in second_transport.sent],
        )

    async def test_pairing_session_does_not_block_m5_delivery(self) -> None:
        source = RecordingSource()
        blocked_gate = asyncio.Event()
        m5_transport = RecordingTransport()
        ws_transport = RecordingTransport(blocked_gate)
        service = MultiDeviceBridgeService(
            source,
            (
                DeviceSession("M5", m5_transport),
                DeviceSession("Waveshare", ws_transport),
            ),
            interval=0,
            clock=lambda: 100,
        )

        await asyncio.wait_for(service.run(cycles=1), timeout=0.1)

        self.assertEqual(source.calls, [100])
        self.assertEqual(len(m5_transport.sent), 2)
        self.assertEqual(ws_transport.sent, [])

    async def test_bounded_run_waits_for_first_available_device(self) -> None:
        m5_gate = asyncio.Event()
        ws_gate = asyncio.Event()
        m5_transport = RecordingTransport(m5_gate)
        ws_transport = RecordingTransport(ws_gate)
        service = MultiDeviceBridgeService(
            RecordingSource(),
            (
                DeviceSession("M5", m5_transport),
                DeviceSession("Waveshare", ws_transport),
            ),
            interval=0,
            clock=lambda: 100,
        )
        run = asyncio.create_task(service.run(cycles=1))
        await asyncio.sleep(0)
        await asyncio.sleep(0)

        self.assertFalse(run.done())
        m5_gate.set()
        await asyncio.wait_for(run, timeout=0.1)

        self.assertEqual(len(m5_transport.sent), 2)
        self.assertEqual(ws_transport.sent, [])

    async def test_collection_failure_offers_unavailable_to_both_sessions(
        self,
    ) -> None:
        source = RecordingSource(fail=True)
        first_transport = RecordingTransport()
        second_transport = RecordingTransport()
        service = MultiDeviceBridgeService(
            source,
            (
                DeviceSession("M5", first_transport),
                DeviceSession("Waveshare", second_transport),
            ),
            interval=0,
            clock=lambda: 100,
        )

        await service.run(cycles=1)

        for transport in (first_transport, second_transport):
            payloads = [json.loads(payload) for payload, _, _ in transport.sent]
            self.assertEqual(
                [payload["state"] for payload in payloads],
                ["unavailable", "unavailable"],
            )

    async def test_shutdown_closes_both_sessions(self) -> None:
        first_transport = RecordingTransport()
        second_transport = RecordingTransport()
        service = MultiDeviceBridgeService(
            RecordingSource(),
            (
                DeviceSession("M5", first_transport),
                DeviceSession("Waveshare", second_transport),
            ),
            interval=0,
            clock=lambda: 100,
        )

        await service.run(cycles=1)

        self.assertTrue(first_transport.closed)
        self.assertTrue(second_transport.closed)


class ImmediateCollectionTests(unittest.IsolatedAsyncioTestCase):
    async def test_request_collection_interrupts_the_live_sleep(self) -> None:
        source = RecordingSource()
        session = DeviceSession("M5", RecordingTransport())
        service = MultiDeviceBridgeService(
            source,
            (session,),
            interval=3600.0,
            publish_interval=3600.0,
        )
        runner = asyncio.create_task(service.run())
        try:
            await asyncio.sleep(0)
            collections_before = service.collection_count
            service.request_collection()
            for _ in range(100):
                await asyncio.sleep(0)
                if service.collection_count > collections_before:
                    break
            self.assertGreater(service.collection_count, collections_before)
        finally:
            runner.cancel()
            await asyncio.gather(runner, return_exceptions=True)


class IdleReconnectTests(unittest.IsolatedAsyncioTestCase):
    async def test_idle_disconnect_replays_without_new_offer(self) -> None:
        transport = RecordingTransport()
        session = DeviceSession("Panel", transport)
        revision = session.offer(snapshot(100))
        task = asyncio.create_task(session.run())
        try:
            await asyncio.wait_for(session.wait_delivered(revision), 1)
            transport.connected = False
            async with asyncio.timeout(2.5):
                while len(transport.sent) < 4:
                    await asyncio.sleep(0.01)
            self.assertTrue(transport.connected)
            self.assertEqual([item[1] for item in transport.sent], ["usage"] * 4)
        finally:
            await session.close()
            await asyncio.gather(task, return_exceptions=True)

    async def test_idle_disconnect_recovers_before_first_collection(self) -> None:
        transport = RecordingTransport()
        session = DeviceSession("Panel", transport)
        task = asyncio.create_task(session.run())
        try:
            async with asyncio.timeout(1):
                while not transport.connected:
                    await asyncio.sleep(0)
            transport.connected = False
            async with asyncio.timeout(2.5):
                while not transport.connected:
                    await asyncio.sleep(0.01)
            self.assertEqual(transport.sent, [])
        finally:
            await session.close()
            await asyncio.gather(task, return_exceptions=True)

    async def test_idle_reconnect_waits_for_exclusive_operation(self) -> None:
        transport = RecordingTransport()
        session = DeviceSession("Panel", transport)
        task = asyncio.create_task(session.run())
        entered = asyncio.Event()
        release = asyncio.Event()

        async def operation(_transport):
            transport.connected = False
            entered.set()
            await release.wait()

        try:
            async with asyncio.timeout(1):
                while not transport.connected:
                    await asyncio.sleep(0)
            exclusive = asyncio.create_task(session.run_exclusive(operation))
            await entered.wait()
            await asyncio.sleep(1.1)
            self.assertFalse(transport.connected)
            release.set()
            await exclusive
            async with asyncio.timeout(2.5):
                while not transport.connected:
                    await asyncio.sleep(0.01)
        finally:
            release.set()
            await session.close()
            await asyncio.gather(task, return_exceptions=True)


if __name__ == "__main__":
    unittest.main()
