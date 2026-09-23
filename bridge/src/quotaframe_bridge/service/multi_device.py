"""Independent device sessions over one shared usage snapshot stream."""

from __future__ import annotations

import asyncio
import logging
import time
from collections.abc import Awaitable, Callable
from dataclasses import dataclass
from datetime import datetime
from typing import TypeVar

from quotaframe_bridge.domain.models import Provider, ProviderUsage, SourceState
from quotaframe_bridge.protocol.messages import (
    MAX_INVERSE_CLOCK_SKEW_SECONDS,
    Sequence,
    encode_screen_toggle,
    encode_screen_page,
    encode_usage,
)
from quotaframe_bridge.service.device_manager import ReconnectBackoff
from quotaframe_bridge.service.state import UsageStateStore
from quotaframe_bridge.service.time_sync import TimeSyncScheduler
from quotaframe_bridge.sources.base import UsageSource
from quotaframe_bridge.sources.codexbar import CodexBarResolutionError
from quotaframe_bridge.transports.base import (
    TransportCleanupError,
    TransportReset,
    UsageTransport,
    device_status,
)

LOGGER = logging.getLogger(__name__)
_SESSION_RESTART_INITIAL_DELAY = 1.0
_SESSION_RESTART_MAX_DELAY = 60.0
T = TypeVar("T")


async def _cancel(task: asyncio.Task[None] | None) -> None:
    """Stop a session-owned background task and wait for it to unwind."""

    if task is not None and not task.done():
        task.cancel()
        await asyncio.gather(task, return_exceptions=True)


@dataclass(frozen=True, slots=True)
class UsageSnapshot:
    """One immutable publication generation for both providers."""

    codex: ProviderUsage
    claude: ProviderUsage


class DeviceSession:
    """Own one transport lifecycle, sequence, and latest-value mailbox."""

    def __init__(
        self,
        label: str,
        transport: UsageTransport,
        *,
        wall_now: Callable[[], datetime] = lambda: datetime.now().astimezone(),
        monotonic: Callable[[], float] = time.monotonic,
    ) -> None:
        self.label = label
        self.transport = transport
        self._sequence = Sequence()
        self._wake = asyncio.Event()
        self._pending: UsageSnapshot | None = None
        self._offered_revision = 0
        self._delivered_revision = 0
        self._delivery_changed = asyncio.Condition()
        self._connected = False
        self._runner: asyncio.Task[None] | None = None
        self._connect_task: asyncio.Task[None] | None = None
        self._exclusive_lock = asyncio.Lock()
        self._exclusive_active = False
        self._exclusive_task: asyncio.Task | None = None
        self._closing = False
        self._wall_now = wall_now
        self._time_sync = TimeSyncScheduler(wall_now, monotonic)
        self._time_sync_task: asyncio.Task[None] | None = None
        self._screen_command_task: asyncio.Task[None] | None = None
        self._force_time_sync = True

    @property
    def connected(self) -> bool:
        return self._connected and getattr(self.transport, "connected", True)

    @property
    def exclusive_active(self) -> bool:
        return self._exclusive_active

    def schedule_screen_toggle(self) -> None:
        self._schedule_screen_command(None)

    def schedule_screen_page(self, direction: int) -> None:
        if direction not in (-1, 1):
            raise ValueError("page direction must be -1 or 1")
        self._schedule_screen_command(direction < 0)

    def _schedule_screen_command(self, previous: bool | None) -> None:
        # Relative commands cannot be replayed safely after an unknown outcome.
        if self._closing or (self._screen_command_task is not None and not self._screen_command_task.done()):
            return
        self._screen_command_task = asyncio.create_task(self._send_screen_command(previous))

    async def _send_screen_command(self, previous: bool | None) -> None:
        capability = "screen.toggle.v1" if previous is None else "screen.page.v1"
        command = "screen_toggle" if previous is None else "screen_page"
        status = device_status(self.transport)
        if (not self.connected or self.exclusive_active or status is None
                or not status.secure or capability not in status.capabilities):
            return
        try:
            async with self._exclusive_lock:
                if (not self.connected or self.exclusive_active
                        or device_status(self.transport) is not status):
                    return
                sequence = self._sequence.next()
                payload = (encode_screen_toggle(sequence) if previous is None
                           else encode_screen_page(sequence, previous))
                await self.transport.send_command(payload, command, sequence)
        except TransportReset:
            self._force_time_sync = True
            LOGGER.warning("%s %s outcome unknown; not replayed", self.label, command)
        except Exception as exc:
            LOGGER.warning("%s %s failed: %s", self.label, command, type(exc).__name__)

    async def _cancel_screen_command(self) -> None:
        await _cancel(self._screen_command_task)
        self._screen_command_task = None

    async def run_exclusive(
        self,
        operation: Callable[[UsageTransport], Awaitable[T]],
        *,
        interrupt_connect: bool = False,
    ) -> T:
        """Run one transport operation without usage publication or time sync.

        In-flight commands finish under their transport timeout before handoff.
        Recovery operations may interrupt negotiation before acquiring the lock.
        An existing exclusive operation is always rejected.
        """

        if self._closing or self._exclusive_active:
            raise RuntimeError("device operation is already active or device is closing")
        self._exclusive_task = asyncio.current_task()
        self._exclusive_active = True
        try:
            if interrupt_connect:
                await self._cancel_time_sync()
                await self._cancel_screen_command()
                if self._connect_task is not None:
                    self._connect_task.cancel()
            async with self._exclusive_lock:
                return await operation(self.transport)
        except asyncio.CancelledError:
            cleanup_error = getattr(self.transport, "cleanup_error", None)
            if cleanup_error is not None:
                raise cleanup_error
            raise
        finally:
            self._exclusive_active = False
            self._exclusive_task = None

    async def _cancel_exclusive(self) -> None:
        if self._exclusive_task is not asyncio.current_task():
            await _cancel(self._exclusive_task)

    def offer(self, snapshot: UsageSnapshot) -> int:
        """Replace the pending snapshot and return its monotonically increasing revision."""

        self._pending = snapshot
        self._offered_revision += 1
        self._wake.set()
        return self._offered_revision

    async def wait_delivered(self, revision: int) -> None:
        """Wait until this session has delivered at least the requested revision."""

        async with self._delivery_changed:
            await self._delivery_changed.wait_for(
                lambda: self._delivered_revision >= revision
            )

    async def _connect(self) -> bool:
        """Allow lifecycle operations to interrupt negotiation without stopping publication."""
        task = asyncio.create_task(self.transport.connect())
        self._connect_task = task
        try:
            await task
            return True
        except asyncio.CancelledError:
            if asyncio.current_task().cancelling():
                raise
            return False
        finally:
            self._connect_task = None

    async def run(self) -> None:
        """Monitor the idle link, reconnect when needed, and publish the latest snapshot."""

        current = asyncio.current_task()
        if current is None:
            raise RuntimeError("device session requires an asyncio task")
        if self._runner is not None and self._runner is not current:
            raise RuntimeError("device session is already running")
        self._runner = current
        self._closing = False
        try:
            while True:
                async with self._exclusive_lock:
                    if await self._connect():
                        break
            self._connected = True
            while True:
                try:
                    await asyncio.wait_for(self._wake.wait(), timeout=1.0)
                except TimeoutError:
                    if self.connected or self._exclusive_active:
                        continue
                    await self._cancel_time_sync()
                    async with self._exclusive_lock:
                        if not await self._connect():
                            continue
                    self._force_time_sync = True
                self._wake.clear()
                snapshot = self._pending
                revision = self._offered_revision
                if snapshot is None:
                    continue
                try:
                    revision = await self._publish(snapshot)
                except TransportReset:
                    self._force_time_sync = True
                    LOGGER.warning(
                        "%s BLE delivery reset; retrying latest snapshot",
                        self.label,
                    )
                    self._wake.set()
                    await asyncio.sleep(0)
                    continue
                async with self._delivery_changed:
                    self._delivered_revision = max(
                        self._delivered_revision,
                        revision,
                    )
                    self._delivery_changed.notify_all()
                self._schedule_time_sync()
        finally:
            self._closing = True
            self._connected = False
            await self._cancel_exclusive()
            await self._cancel_screen_command()
            await self._cancel_time_sync()
            await self.transport.close()
            if self._pending is not None:
                self._wake.set()

    async def close(self) -> None:
        """Cancel the session runner when owned elsewhere, then close its transport."""

        self._closing = True
        self._connected = False
        await self._cancel_exclusive()
        await self._cancel_screen_command()
        runner = self._runner
        current = asyncio.current_task()
        if runner is not None and runner is not current and not runner.done():
            runner.cancel()
            await asyncio.gather(runner, return_exceptions=True)
            return
        if runner is None:
            await self.transport.close()

    async def _publish(self, snapshot: UsageSnapshot) -> int:
        async with self._exclusive_lock:
            if not getattr(self.transport, "connected", True):
                if not await self._connect():
                    raise TransportReset("connection interrupted for device recovery")
                self._force_time_sync = True
            snapshot = self._pending or snapshot
            revision = self._offered_revision
            for usage in (snapshot.codex, snapshot.claude):
                sequence = self._sequence.next()
                sent_at = int(self._wall_now().timestamp())
                if sent_at + MAX_INVERSE_CLOCK_SKEW_SECONDS < usage.sampled_at:
                    # A clock rollback can make the cached timestamp invalid on
                    # the wire. Unavailable preserves the device's cached sample.
                    usage = ProviderUsage(usage.provider, SourceState.UNAVAILABLE, sent_at)
                payload = encode_usage(usage, sequence, sent_at)
                await self.transport.send_command(payload, "usage", sequence)
            return revision

    def _schedule_time_sync(self) -> None:
        if self._time_sync_task is not None and not self._time_sync_task.done():
            return
        self._time_sync_task = asyncio.create_task(self._sync_time())

    async def _cancel_time_sync(self) -> None:
        await _cancel(self._time_sync_task)
        self._time_sync_task = None

    async def _sync_time(self) -> None:
        if not self._time_sync.is_supported(device_status(self.transport)):
            self._force_time_sync = False
            return
        try:
            async with self._exclusive_lock:
                if self._closing or self._exclusive_active:
                    return
                delivered = await self._time_sync.sync_if_due(
                    self.transport,
                    self._sequence,
                    force=self._force_time_sync,
                )
                if delivered:
                    self._force_time_sync = False
        except TransportReset:
            self._force_time_sync = True
            LOGGER.debug("%s time sync reset; will retry", self.label)
        except Exception:
            LOGGER.debug("%s time sync not delivered", self.label)


class MultiDeviceBridgeService:
    """Collect once and offer the latest state to independent device sessions."""

    def __init__(
        self,
        source: UsageSource,
        sessions: tuple[DeviceSession, ...],
        *,
        interval: float = 60.0,
        publish_interval: float = 30.0,
        clock: Callable[[], int] = lambda: int(time.time()),
        monotonic: Callable[[], float] = time.monotonic,
        store: UsageStateStore | None = None,
    ) -> None:
        # Zero sessions is a legitimate steady state, not a misconfiguration:
        # a user who has not adopted a board yet still gets usage collection
        # and tray percentages, and must not get a background scan for boards
        # nobody owns.
        if interval < 0 or publish_interval <= 0:
            raise ValueError(
                "interval must be non-negative and publish interval positive"
            )
        self.source = source
        self.sessions = sessions
        self.interval = interval
        self.publish_interval = publish_interval
        self.clock = clock
        self.monotonic = monotonic
        self.store = UsageStateStore() if store is None else store
        self._first_collection = True
        self._collect_now = asyncio.Event()
        self._supervisors: dict[DeviceSession, asyncio.Task[None]] = {}
        self._running = False
        self.collection_count = 0
        self.consecutive_failures = 0
        self.resolution_error: str | None = None

    def toggle_screensavers(self) -> None:
        """Apply one tray click independently to each eligible device."""

        for session in self.sessions:
            session.schedule_screen_toggle()

    def turn_pages(self, direction: int) -> None:
        """Apply one wheel step to every connected, capable device."""
        for session in self.sessions:
            session.schedule_screen_page(direction)

    def request_collection(self) -> None:
        """Ask the live loop to collect now instead of after the interval."""

        self._collect_now.set()

    async def collect(self) -> None:
        """Collect once, retaining last-valid values separately from publish state."""

        attempted_at = self.clock()
        # Collection is the one step in the loop with no visible progress and no
        # obvious upper bound: on macOS the upstream CodexBar CLI performs live
        # web fetches and can take tens of seconds, which without these lines is
        # indistinguishable from a hang. Only the first one is announced at INFO,
        # because a steady-state loop logging every minute is noise in a rotating
        # log that runs for days.
        level = logging.INFO if self._first_collection else logging.DEBUG
        LOGGER.log(level, "reading usage from CodexBar")
        started_at = self.monotonic()
        try:
            collection = await self.source.collect(attempted_at=attempted_at)
            self.store.merge_collection(
                collection,
                attempted_at=attempted_at,
            )
            self.consecutive_failures = 0
            self.resolution_error = None
        except CodexBarResolutionError as exc:
            LOGGER.warning("usage source unavailable: %s", exc.category)
            self.consecutive_failures += 1
            self.resolution_error = exc.category
            self.store.mark_all_unavailable(attempted_at=attempted_at)
        except Exception as exc:
            LOGGER.warning(
                "usage collection unavailable: %s",
                type(exc).__name__,
            )
            self.consecutive_failures += 1
            self.store.mark_all_unavailable(attempted_at=attempted_at)
        finally:
            LOGGER.log(
                level,
                "usage collection finished in %.1fs",
                self.monotonic() - started_at,
            )
            self._first_collection = False
        self.collection_count += 1

    async def run(self, cycles: int | None = None) -> None:
        """Run all device sessions with either bounded or continuous collection."""

        if cycles is not None and cycles < 1:
            raise ValueError("cycles must be at least one")
        self._running = True
        for session in self.sessions:
            self._start_supervisor(session)
        try:
            if cycles is None:
                await self._run_live()
                return
            for completed in range(cycles):
                await self.collect()
                offers = self._offer_current()
                await self._wait_for_available_deliveries(offers)
                if completed + 1 < cycles:
                    await asyncio.sleep(self.interval)
        finally:
            self._running = False
            await asyncio.gather(
                *(session.close() for session in self.sessions),
                return_exceptions=True,
            )
            tasks = tuple(self._supervisors.values())
            self._supervisors.clear()
            for task in tasks:
                task.cancel()
            await asyncio.gather(*tasks, return_exceptions=True)

    def _start_supervisor(self, session: DeviceSession) -> None:
        if session in self._supervisors:
            return
        self._supervisors[session] = asyncio.create_task(
            self._supervise_session(session)
        )

    def add_session(self, session: DeviceSession) -> None:
        """Attach a session to a service that may already be running.

        Adoption happens while the tray is live, so a newly owned board has to
        join the running graph rather than wait for a restart.
        """

        if session in self.sessions:
            return
        self.sessions = self.sessions + (session,)
        providers = self.store.providers
        if providers:
            session.offer(UsageSnapshot(
                codex=providers[Provider.CODEX],
                claude=providers[Provider.CLAUDE],
            ))
        if self._running:
            self._start_supervisor(session)

    async def remove_session(self, session: DeviceSession) -> None:
        """Detach and close one session, stopping its supervised restarts.

        The supervisor is cancelled before the session is closed: cancelling
        second would let it observe the close as an unexpected stop and
        schedule a reconnect for a board the user just removed.
        """

        self.sessions = tuple(
            existing for existing in self.sessions if existing is not session
        )
        task = self._supervisors.pop(session, None)
        if task is not None and not task.done():
            task.cancel()
            await asyncio.gather(task, return_exceptions=True)
        await session.close()

    async def _supervise_session(self, session: DeviceSession) -> None:
        backoff = ReconnectBackoff(
            initial=_SESSION_RESTART_INITIAL_DELAY,
            maximum=_SESSION_RESTART_MAX_DELAY,
        )
        while True:
            try:
                await session.run()
            except asyncio.CancelledError:
                raise
            except TransportCleanupError as exc:
                LOGGER.error("%s: %s", session.label, exc)
                return
            except Exception:
                restart_delay = backoff.next_delay()
                LOGGER.exception(
                    "%s session stopped unexpectedly; restarting in %.1fs",
                    session.label,
                    restart_delay,
                )
            else:
                restart_delay = backoff.next_delay()
                LOGGER.error(
                    "%s session stopped unexpectedly; restarting in %.1fs",
                    session.label,
                    restart_delay,
                )
            await asyncio.sleep(restart_delay)

    async def _wait_for_available_deliveries(
        self,
        offers: tuple[tuple[DeviceSession, int], ...],
    ) -> None:
        # Sessions are paired with their own revision at offer time rather
        # than re-zipped against self.sessions here, because adoption and
        # removal can change the session set between the two points.
        if not offers:
            return
        waiters = tuple(
            (session, asyncio.create_task(session.wait_delivered(revision)))
            for session, revision in offers
        )
        try:
            await asyncio.wait(
                (waiter for _, waiter in waiters),
                return_when=asyncio.FIRST_COMPLETED,
            )
            connected_waiters = tuple(
                waiter
                for session, waiter in waiters
                if session.connected and not waiter.done()
            )
            if connected_waiters:
                await asyncio.gather(*connected_waiters)
        finally:
            pending = tuple(
                waiter
                for _, waiter in waiters
                if not waiter.done()
            )
            for waiter in pending:
                waiter.cancel()
            if pending:
                await asyncio.gather(*pending, return_exceptions=True)

    async def _run_live(self) -> None:
        async with asyncio.TaskGroup() as tasks:
            tasks.create_task(self._publish_periodically())
            while True:
                self._collect_now.clear()
                await self.collect()
                self._offer_current()
                try:
                    await asyncio.wait_for(self._collect_now.wait(), self.interval)
                except TimeoutError:
                    pass

    async def _publish_periodically(self) -> None:
        while True:
            await asyncio.sleep(self.publish_interval)
            self._offer_current()

    def _offer_current(self) -> tuple[tuple[DeviceSession, int], ...]:
        providers = self.store.providers
        if not providers:
            return ()
        snapshot = UsageSnapshot(
            codex=providers[Provider.CODEX],
            claude=providers[Provider.CLAUDE],
        )
        return tuple(
            (session, session.offer(snapshot)) for session in self.sessions
        )
