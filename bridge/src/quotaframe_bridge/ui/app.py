"""Tray-resident entry point for the Bridge."""

from __future__ import annotations

import ctypes
import os
import sys

from quotaframe_bridge import __version__

# PROCESS_SYSTEM_DPI_AWARE. Per-monitor awareness would be the modern choice,
# but Tk 8.6 does not re-scale when a window crosses to a different display,
# which trades blur everywhere for wrong sizes on the second monitor.
DPI_SYSTEM_AWARE = 1


def enable_dpi_awareness() -> None:
    """Render at the display's real resolution instead of being stretched.

    An unaware process is laid out at 96 DPI and then bitmap-scaled by
    Windows, which softens every glyph in the dialogs and in the tray menu.
    This has to run before the first window exists, and Windows refuses a
    second call once awareness is set, which is not an error worth dying on.
    """

    try:
        ctypes.windll.shcore.SetProcessDpiAwareness(DPI_SYSTEM_AWARE)
    except (AttributeError, OSError):
        try:
            ctypes.windll.user32.SetProcessDPIAware()
        except (AttributeError, OSError):
            pass


def _silence_missing_streams() -> None:
    """Give a windowed build real file objects for stdout and stderr.

    PyInstaller's --windowed mode leaves both as None. Any library that
    writes a single byte to stderr would otherwise raise AttributeError,
    and with no console there would be nothing to see.
    """

    for name in ("stdout", "stderr"):
        if getattr(sys, name, None) is None:
            setattr(sys, name, open(os.devnull, "w", encoding="utf-8"))


_silence_missing_streams()
enable_dpi_awareness()

import asyncio
import concurrent.futures
import logging
import threading
import time
import tkinter as tk
import webbrowser
from collections.abc import Callable

from quotaframe_bridge.transports.base import TransportCleanupError
from quotaframe_bridge.i18n import tr
from quotaframe_bridge.cli.instance_lock import (
    BridgeAlreadyRunningError,
    BridgeInstanceLock,
)
from quotaframe_bridge.pairing.windows import WindowsPairer
from quotaframe_bridge.release_config import release_repository
from quotaframe_bridge.service.firmware_update import FirmwareUpdateMonitor
from quotaframe_bridge.service.multi_device import MultiDeviceBridgeService
from quotaframe_bridge.service.update import (
    BridgeUpdateMonitor,
    UpdateState,
)
from quotaframe_bridge.sources.selection import create_usage_source
from quotaframe_bridge.sources.firmware_release import (
    FirmwareImage,
    FirmwareReleaseSource,
)
from quotaframe_bridge.sources.product_release import (
    ProductReleaseSource,
)
from quotaframe_bridge.ui import (
    TrayServiceGraph,
    build_tray_graph,
    run_adoption,
    snapshot_tray_status,
)
from quotaframe_bridge.ui.autostart import disable, enable, is_enabled
from quotaframe_bridge.ui.logging_setup import configure_logging, log_path
from quotaframe_bridge.ui.update_presenter import (
    BrowserOpen,
    UpdatePresenter,
)
from quotaframe_bridge.ui.worker_cleanup import cleanup_worker_tasks
from quotaframe_bridge.ui.firmware_actions import (
    FIRMWARE_MANIFEST_ENV, FirmwareUpdateActions,
)
from quotaframe_bridge.ui.controller import TrayController
from quotaframe_bridge.ui.windows_installation import is_installed
from quotaframe_bridge.ui.notifications import NotificationKind, NotificationPolicy
from quotaframe_bridge.ui.pystray_shell import PystrayShell
from quotaframe_bridge.ui.tk_pin_prompt import TkPinPrompt
from quotaframe_bridge.versioning import SemVer

LOGGER = logging.getLogger(__name__)

STATUS_INTERVAL_S = 5.0
RESTART_DELAY_S = 30.0

MonitorFactory = Callable[..., BridgeUpdateMonitor]


class TrayApplication(FirmwareUpdateActions):
    """Own the three threads and the wiring between them."""

    def __init__(
        self,
        *,
        release_source: ProductReleaseSource | None = None,
        monitor_factory: MonitorFactory | None = None,
        browser: BrowserOpen | None = None,
    ) -> None:
        product_source = (
            ProductReleaseSource(release_repository(os.environ))
            if release_source is None
            else release_source
        )
        self._root = tk.Tk()
        self._root.withdraw()
        self._prompt = TkPinPrompt(self._root)
        self._policy = NotificationPolicy()
        self._loop: asyncio.AbstractEventLoop | None = None
        self._service: MultiDeviceBridgeService | None = None
        self._graph: TrayServiceGraph[WindowsPairer] | None = None
        self._adoption_running = False
        self._stopping = False
        self._shutdown_lock = threading.Lock()
        self._firmware_updates = 0
        self._log_path = log_path()
        self._shell = PystrayShell(
            on_refresh=self._refresh,
            on_toggle_screensaver=self._toggle_screensaver,
            on_turn_page=self._turn_page,
            on_add_device=self._add_device,
            on_repair=self._repair,
            on_firmware_update=self._firmware_update,
            on_forget_device=self._forget_device,
            on_toggle_autostart=self._toggle_autostart,
            on_open_log=self._open_log,
            on_quit=self._quit,
            can_auto_quit=self._can_auto_quit,
            on_auto_quit=self._auto_quit,
            on_bridge_update=self._bridge_update,
            # Starts empty: the owned set is only known once the worker
            # thread has read devices.json, and grows again on adoption.
            device_labels=(),
        )
        self._controller = TrayController(self._shell, self._policy)
        self._firmware_update_monitor = FirmwareUpdateMonitor(
            FirmwareReleaseSource(),
            self._firmware_manifest_url,
        )
        self._update_presenter = UpdatePresenter(
            self._controller,
            webbrowser.open if browser is None else browser,
            dispatch=self._prompt.call_soon,
        )
        make_monitor = (
            BridgeUpdateMonitor if monitor_factory is None else monitor_factory
        )
        self._update_monitor = make_monitor(
            product_source,
            SemVer.parse(__version__),
            self._update_presenter,
            windows_installed=is_installed(),
        )
        self._controller.set_autostart_checked(is_enabled())

    def _turn_page(self, direction: int) -> None:
        loop, service = self._loop, self._service
        if loop is not None and service is not None and not self._stopping:
            loop.call_soon_threadsafe(service.turn_pages, direction)

    def _toggle_screensaver(self) -> None:
        loop, service = self._loop, self._service
        if loop is None or service is None or self._stopping:
            return
        loop.call_soon_threadsafe(service.toggle_screensavers)

    def _refresh(self) -> None:
        loop, service = self._loop, self._service
        if loop is None or service is None:
            return
        loop.call_soon_threadsafe(service.request_collection)

    def _bridge_update(self) -> None:
        if self._update_presenter.open_page():
            return
        loop = self._loop
        if loop is None:
            return
        future = asyncio.run_coroutine_threadsafe(
            self._update_monitor.check(manual=True), loop
        )

        def report_failure(completed: concurrent.futures.Future[object]) -> None:
            try:
                completed.result()
            except concurrent.futures.CancelledError:
                pass
            except Exception as exc:
                LOGGER.warning(
                    "manual update check failed: %s", type(exc).__name__
                )

        future.add_done_callback(report_failure)

    def _add_device(self) -> None:
        loop, graph = self._loop, self._graph
        if loop is None or graph is None:
            return
        future = asyncio.run_coroutine_threadsafe(self._run_adoption(graph), loop)

        def report_failure(completed: concurrent.futures.Future[object]) -> None:
            try:
                completed.result()
            except Exception:
                LOGGER.exception("manual device adoption failed")

        future.add_done_callback(report_failure)

    def _repair(self, address: str) -> None:
        label = address
        loop, graph = self._loop, self._graph
        if loop is None or graph is None:
            LOGGER.warning("repair request ignored before worker loop starts: %s", label)
            return
        device = graph.get(address)
        if device is None:
            LOGGER.warning("repair target unavailable: device=%s", label)
            return
        label = device.name
        LOGGER.info("repair request submitted: device=%s", label)
        future = asyncio.run_coroutine_threadsafe(
            self._repair_async(label, device.address, expected=device), loop
        )

        def report_failure(completed: concurrent.futures.Future[object]) -> None:
            try:
                completed.result()
            except Exception:
                LOGGER.exception("repair request failed: device=%s", label)

        future.add_done_callback(report_failure)

    async def _confirm_firmware(self, device, status, image) -> bool:
        return await self._prompt.confirm_firmware(self._firmware_confirmation_text(device, status, image))

    def _set_firmware_action(
        self, label: str, enabled: bool, detail: str
    ) -> None:
        graph = self._graph
        device = None if graph is None else graph.get(label)
        self._prompt.call_soon(
            lambda: self._controller.set_firmware_action(label, enabled, detail)
            if graph is not None and self._graph is graph and graph.get(label) is device else None
        )

    def _notify_firmware(self, title: str, body: str) -> None:
        self._prompt.call_soon(
            lambda: self._controller.notify(title, body)
        )

    async def _repair_async(self, label: str, address: str, *, expected) -> None:
        graph = self._graph
        device = None if graph is None else graph.get(address)
        if device is None or device is not expected:
            return
        LOGGER.info("repair confirmation queued: device=%s", label)
        confirmed = await self._prompt.confirm_repair(label)
        LOGGER.info("repair confirmation answered: device=%s confirmed=%s", label, confirmed)
        if not confirmed:
            return
        if self._graph is not graph or graph.get(address) is not device:
            LOGGER.warning("repair target unavailable: device=%s", label)
            return
        async def repair(transport):
            device.pairer.request_repair()
            await transport.reset()

        try:
            await device.session.run_exclusive(repair, interrupt_connect=True)
        except TransportCleanupError:
            self._notify_firmware(tr("firmware_failed"), tr("ble_cleanup_failed"))
            return
        except RuntimeError:
            self._notify_firmware(tr("firmware_failed"), tr("updating"))
            return
        if self._service is not None:
            self._service.request_collection()

    def _toggle_autostart(self, desired: bool) -> None:
        if desired:
            enable()
        else:
            disable()
        self._controller.set_autostart_checked(is_enabled())

    def _open_log(self) -> None:
        try:
            os.startfile(self._log_path)  # noqa: S606 - user-initiated
        except OSError:
            LOGGER.warning("could not open the log file")

    def _can_auto_quit(self) -> bool:
        with self._shutdown_lock:
            return self._firmware_updates == 0

    def _auto_quit(self) -> None:
        # OTA admission and automatic shutdown must be atomic across threads.
        with self._shutdown_lock:
            if self._firmware_updates:
                return
            self._quit()

    def _quit(self) -> None:
        if self._stopping:
            return
        self._stopping = True
        loop = self._loop
        if loop is not None:
            loop.call_soon_threadsafe(loop.stop)
        self._controller.stop()
        # Order matters: a dialog runs a nested Tk loop that quit cannot break
        # out of, so it has to be destroyed before the main loop is asked to
        # end. Both go through the queue because Tk belongs to the main thread.
        self._prompt.call_soon(self._prompt.close_dialogs)
        self._prompt.call_soon(self._root.quit)

    async def _ask_pin(self, device_name: str) -> str:
        self._prompt.call_soon(
            lambda: self._controller.announce(
                NotificationKind.PAIRING_REQUIRED,
                time.monotonic(),
            )
        )
        return await self._prompt.ask_pin(device_name)

    def _dependency_status(self, status: str) -> None:
        LOGGER.info("managed CLI preparation: %s", status)
        self._prompt.call_soon(
            lambda: self._controller.notify(
                tr("dependency_title"), tr(f"dependency_{status}")
            )
        )

    def _build_service(self) -> MultiDeviceBridgeService:
        # One lock for every pairer, including the ones adoption creates
        # later: Windows refuses a second PROVIDE_PIN ceremony while one is
        # open, so two boards pairing at once would fail one of them.
        ceremony_lock = asyncio.Lock()
        graph = build_tray_graph(
            usage_source_factory=lambda: create_usage_source(
                None, dependency_status=self._dependency_status
            ),
            pairer_factory=lambda: WindowsPairer(
                self._ask_pin,
                repair=False,
                ceremony_lock=ceremony_lock,
            ),
        )
        self._graph = graph
        return graph.service

    async def _publish_status(self, graph: TrayServiceGraph[WindowsPairer]) -> None:
        while True:
            snapshot = snapshot_tray_status(
                graph, adoption_running=self._adoption_running
            )
            now = time.monotonic()
            self._prompt.call_soon(
                lambda snapshot=snapshot, now=now: self._apply_snapshot(
                    snapshot, now
                ) if self._graph is graph else None
            )
            self._publish_firmware_actions(graph)
            await asyncio.sleep(STATUS_INTERVAL_S)

    def _apply_snapshot(self, snapshot, now: float) -> None:
        """Push one snapshot to the tray. Runs on Tk's thread.

        Device rows go first: the menu a snapshot describes must exist before
        the shared summary is published.
        """

        self._shell.set_device_statuses(snapshot.devices)
        self._controller.apply(snapshot, now)

    async def _monitor_updates(self) -> None:
        try:
            await self._update_monitor.run()
        except asyncio.CancelledError:
            raise
        except Exception as exc:
            LOGGER.warning(
                "automatic update monitor stopped: %s", type(exc).__name__
            )

    async def _run_adoption(
        self, graph: TrayServiceGraph[WindowsPairer]
    ) -> None:
        if self._adoption_running:
            return
        self._adoption_running = True
        try:
            await run_adoption(graph, on_adopted=self._announce_adoption)
        except asyncio.CancelledError:
            raise
        except Exception:
            LOGGER.exception("device adoption stopped unexpectedly")
        finally:
            self._adoption_running = False

    def _announce_adoption(self, device: object) -> None:
        name = getattr(device, "name", tr("panel"))
        LOGGER.info("device adopted: %s", name)
        self._notify_firmware(tr("device_added"), tr("device_added_body", name=name))

    def _forget_device(self, address: str) -> None:
        loop, graph = self._loop, self._graph
        if loop is None or graph is None:
            return
        device = graph.get(address)
        if device is None:
            return
        label = device.name
        LOGGER.info("forget device requested: device=%s", label)
        future = asyncio.run_coroutine_threadsafe(
            self._forget_async(graph, label, device.address, expected=device), loop
        )

        def report_failure(completed: concurrent.futures.Future[object]) -> None:
            try:
                completed.result()
            except Exception:
                LOGGER.exception("forget device failed: device=%s", label)

        future.add_done_callback(report_failure)

    async def _forget_async(
        self,
        graph: TrayServiceGraph[WindowsPairer],
        label: str,
        address: str,
        *, expected,
    ) -> None:
        device = graph.get(address)
        if device is None or device is not expected:
            return
        confirmed = await self._prompt.confirm_forget(label)
        if not confirmed:
            return
        if await graph.forget(address, expected=device):
            self._notify_firmware(tr("device_removed"), tr("device_removed_body", label=label))

    async def _run_background_tasks(
        self,
        service: MultiDeviceBridgeService,
        graph: TrayServiceGraph[WindowsPairer],
    ) -> None:
        await asyncio.gather(
            service.run(),
            self._publish_status(graph),
            self._monitor_updates(),
            self._monitor_firmware_updates(),
        )

    def _worker(self) -> None:
        loop = asyncio.new_event_loop()
        asyncio.set_event_loop(loop)
        self._loop = loop
        reported = False
        try:
            while not self._stopping:
                service = self._build_service()
                self._service = service
                try:
                    loop.run_until_complete(
                        self._run_background_tasks(service, self._graph)
                    )
                except Exception:
                    LOGGER.exception("bridge service stopped unexpectedly")
                    if not reported:
                        reported = True
                        self._prompt.call_soon(
                            lambda: self._controller.announce(
                                NotificationKind.STARTUP_FAILED,
                                time.monotonic(),
                            )
                        )
                finally:
                    self._loop = None
                    self._service = None
                    self._graph = None
                    clean = cleanup_worker_tasks(loop)
                if self._stopping or not clean:
                    break
                time.sleep(RESTART_DELAY_S)
                if not self._stopping:
                    self._loop = loop
        finally:
            self._loop = None
            loop.close()

    def run(self) -> None:
        threading.Thread(target=self._worker, daemon=False).start()
        # pystray's blocking run loop must not be moved to an arbitrary
        # worker: Tk needs this process's main thread for its event loop.
        # Its detached integration starts the Windows tray pump and returns.
        self._shell.run()
        self._root.mainloop()


def main(argv: list[str] | None = None) -> int:
    """Entry point. Never calls parser.error: there is no console to see it."""

    arguments = sys.argv[1:] if argv is None else argv
    if "--version" in arguments:
        print(f"QuotaFrame Bridge {__version__}")
        return 0
    unknown = [item for item in arguments if item != "--autostarted"]
    configure_logging()
    if unknown:
        LOGGER.warning("ignoring unrecognised arguments: %s", " ".join(unknown))
    try:
        with BridgeInstanceLock.acquire():
            TrayApplication().run()
    except BridgeAlreadyRunningError:
        LOGGER.error("another instance already holds the lock")
        root = tk.Tk()
        root.withdraw()
        TkPinPrompt(root).show_already_running()
        root.destroy()
        return 1
    except Exception:
        LOGGER.exception("tray application failed to start")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
