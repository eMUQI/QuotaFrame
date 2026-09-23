from __future__ import annotations

import asyncio
import json
import unittest
from datetime import datetime, timedelta, timezone

from quotaframe_bridge.protocol.messages import (
    AckError,
    DeviceStatus,
    Sequence,
    encode_time_sync,
)
from quotaframe_bridge.domain.models import Provider, ProviderUsage, SourceState
from quotaframe_bridge.service.multi_device import DeviceSession, UsageSnapshot
from quotaframe_bridge.service.time_sync import TimeSyncScheduler


class MutableMonotonic:
    def __init__(self, value: float = 0.0) -> None:
        self.value = value

    def __call__(self) -> float:
        return self.value


class RecordingTransport:
    def __init__(self, *, reject: bool = False) -> None:
        self.reject = reject
        self.sent: list[tuple[bytes, str, int]] = []
        self.device_status = status("time.sync.v1")
        self.time_sync_sent = asyncio.Event()

    async def connect(self) -> None:
        return None

    async def close(self) -> None:
        return None

    async def send_command(
        self,
        payload: bytes,
        command: str,
        expected_ack_n: int,
    ) -> None:
        self.sent.append((payload, command, expected_ack_n))
        if command == "time_sync":
            self.time_sync_sent.set()
        if self.reject:
            raise AckError("rejected")


def status(*capabilities: str) -> DeviceStatus:
    return DeviceStatus(
        name="Waveshare Usage Panel",
        secure=True,
        protocol=1,
        page="overview",
        capabilities=frozenset(capabilities),
        target="waveshare_amoled_216",
    )


class TimeSyncEncodingTests(unittest.TestCase):
    def test_encoder_emits_exact_local_calendar_fields(self) -> None:
        local = datetime(
            2028,
            2,
            29,
            23,
            58,
            59,
            tzinfo=timezone(timedelta(hours=8)),
        )

        payload = json.loads(encode_time_sync(local, 42))

        self.assertEqual(
            payload,
            {
                "cmd": "time_sync",
                "v": "1",
                "seq": "42",
                "year": "2028",
                "month": "2",
                "day": "29",
                "weekday": "2",
                "hour": "23",
                "minute": "58",
                "second": "59",
                "utc_offset_min": "480",
            },
        )

    def test_encoder_requires_timezone_aware_supported_calendar(self) -> None:
        with self.assertRaises(ValueError):
            encode_time_sync(datetime(2028, 2, 29, 1, 2, 3), 1)
        with self.assertRaises(ValueError):
            encode_time_sync(
                datetime(2100, 1, 1, tzinfo=timezone.utc),
                1,
            )


class TimeSyncSchedulerTests(unittest.IsolatedAsyncioTestCase):
    async def test_acknowledged_sync_is_due_again_after_six_hours(self) -> None:
        monotonic = MutableMonotonic(10.0)
        wall_time = datetime(2028, 2, 29, 23, 58, tzinfo=timezone.utc)
        transport = RecordingTransport()
        scheduler = TimeSyncScheduler(lambda: wall_time, monotonic)
        sequence = Sequence(initial=7)

        self.assertTrue(scheduler.is_supported(status("time.sync.v1")))
        self.assertTrue(
            await scheduler.sync_if_due(transport, sequence, force=True)
        )
        monotonic.value = 10.0 + 21_599
        self.assertFalse(await scheduler.sync_if_due(transport, sequence))
        monotonic.value += 1
        self.assertTrue(await scheduler.sync_if_due(transport, sequence))

        self.assertEqual(
            [(command, seq) for _, command, seq in transport.sent],
            [("time_sync", 7), ("time_sync", 8)],
        )

    async def test_rejection_does_not_advance_due_deadline(self) -> None:
        monotonic = MutableMonotonic()
        transport = RecordingTransport(reject=True)
        scheduler = TimeSyncScheduler(
            lambda: datetime(2028, 1, 1, tzinfo=timezone.utc),
            monotonic,
        )
        sequence = Sequence()

        self.assertFalse(
            await scheduler.sync_if_due(transport, sequence, force=True)
        )
        transport.reject = False
        self.assertTrue(await scheduler.sync_if_due(transport, sequence))
        self.assertEqual([item[2] for item in transport.sent], [0, 1])

    def test_m5_capabilities_are_not_supported(self) -> None:
        scheduler = TimeSyncScheduler(
            lambda: datetime(2028, 1, 1, tzinfo=timezone.utc),
            lambda: 0.0,
        )

        self.assertFalse(scheduler.is_supported(status("usage.v1")))


class UnavailableSource:
    async def collect(self, *, attempted_at: int):
        return {
            provider: ProviderUsage(
                provider,
                SourceState.UNAVAILABLE,
                attempted_at,
            )
            for provider in Provider
        }


class DeviceSessionTimeSyncTests(unittest.IsolatedAsyncioTestCase):
    async def test_capable_session_delivers_usage_before_sync(self) -> None:
        transport = RecordingTransport()
        session = DeviceSession(
            "Waveshare",
            transport,
            wall_now=lambda: datetime(2028, 2, 29, tzinfo=timezone.utc),
            monotonic=lambda: 0.0,
        )
        providers = await UnavailableSource().collect(attempted_at=1_800_000_000)
        snapshot = UsageSnapshot(
            codex=providers[Provider.CODEX],
            claude=providers[Provider.CLAUDE],
        )
        runner = asyncio.create_task(session.run())
        try:
            revision = session.offer(snapshot)
            await session.wait_delivered(revision)
            await asyncio.wait_for(transport.time_sync_sent.wait(), timeout=0.1)
        finally:
            runner.cancel()
            await asyncio.gather(runner, return_exceptions=True)

        self.assertEqual(
            [command for _, command, _ in transport.sent],
            ["usage", "usage", "time_sync"],
        )
