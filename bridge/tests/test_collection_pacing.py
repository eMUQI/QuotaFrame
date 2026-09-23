"""`--interval` must be a gap between collections, not a period.

The distinction is invisible when collection is fast, but a slow source can
consume a substantial fraction of the configured interval. Scheduling the next
collection from the previous start would then collapse the intended idle gap
and keep the source helper running almost continuously. The 46-second fixture
below is a representative slow collection, not a performance expectation.
"""

from __future__ import annotations

import asyncio
import unittest
from unittest.mock import patch

from quotaframe_bridge.domain.models import Provider, ProviderUsage, SourceState
from quotaframe_bridge.service.multi_device import (
    DeviceSession,
    MultiDeviceBridgeService,
)

SLOW_COLLECTION = 46.0
INTERVAL = 60.0


class FakeClock:
    """A monotonic clock the test advances by hand."""

    def __init__(self) -> None:
        self.now = 0.0

    def __call__(self) -> float:
        return self.now


class SlowSource:
    """Advances the clock the way a deliberately slow collection would."""

    def __init__(self, clock: FakeClock) -> None:
        self._clock = clock
        self.started_at: list[float] = []

    async def collect(self, attempted_at: int | None = None):
        self.started_at.append(self._clock.now)
        self._clock.now += SLOW_COLLECTION
        return {
            provider: ProviderUsage(
                provider=provider,
                state=SourceState.UNAVAILABLE,
                sampled_at=0,
            )
            for provider in Provider
        }


class RecordingTransport:
    def __init__(self) -> None:
        self.sent = 0

    async def connect(self) -> None:
        return None

    async def send_command(self, payload, command, expected_ack_n) -> None:
        self.sent += 1

    async def close(self) -> None:
        return None


class CollectionPacingTests(unittest.IsolatedAsyncioTestCase):
    async def test_first_collection_can_outlast_publication_interval(self):
        release = asyncio.Event()
        class FirstSlowSource:
            async def collect(self, **kwargs):
                await release.wait()
                return {}
        transport = RecordingTransport()
        service = MultiDeviceBridgeService(FirstSlowSource(),
            (DeviceSession("panel", transport),), publish_interval=.005)
        task = asyncio.create_task(service.run())
        try:
            await asyncio.sleep(.02)
            self.assertFalse(task.done())
            self.assertEqual(transport.sent, 0)
            release.set()
            for _ in range(20):
                await asyncio.sleep(.005)
                if transport.sent:
                    break
            self.assertGreater(transport.sent, 0)
        finally:
            task.cancel()
            await asyncio.gather(task, return_exceptions=True)

    async def test_slow_collection_does_not_stop_periodic_publication(self):
        entered = asyncio.Event()
        published = asyncio.Event()
        class BlockedSource:
            async def collect(self, **kwargs):
                entered.set()
                await asyncio.Event().wait()
        service = MultiDeviceBridgeService(BlockedSource(), (), publish_interval=.01)
        offers = []
        def offer():
            offers.append(True)
            if len(offers) >= 2:
                published.set()
            return ()
        service._offer_current = offer
        task = asyncio.create_task(service._run_live())
        try:
            await asyncio.wait_for(entered.wait(), .5)
            await asyncio.wait_for(published.wait(), .5)
            self.assertEqual(service.collection_count, 0)
        finally:
            task.cancel()
            await asyncio.gather(task, return_exceptions=True)

    async def test_interval_is_measured_from_the_end_of_a_slow_collection(
        self,
    ) -> None:
        clock = FakeClock()
        source = SlowSource(clock)
        service = MultiDeviceBridgeService(
            source,
            (DeviceSession("Panel", RecordingTransport()),),
            interval=INTERVAL,
            publish_interval=30.0,
            monotonic=clock,
        )

        async def wait_for(waiter, timeout: float) -> None:
            waiter.close()
            clock.now += timeout
            if len(source.started_at) >= 3:
                raise asyncio.CancelledError
            raise TimeoutError

        with self.assertRaises(asyncio.CancelledError):
            with patch("asyncio.wait_for", wait_for):
                await service._run_live()

        gaps = [
            later - (earlier + SLOW_COLLECTION)
            for earlier, later in zip(source.started_at, source.started_at[1:])
        ]
        for gap in gaps:
            self.assertAlmostEqual(gap, INTERVAL, places=1)


if __name__ == "__main__":
    unittest.main()
