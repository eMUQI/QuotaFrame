"""Menu-bar-resident entry point for the macOS Bridge.

Two threads, same shape as the Windows tray application:

- the main thread belongs to AppKit, which is not negotiable — NSApplication's
  run loop has to own it;
- a worker thread runs the asyncio service.

Unlike the Windows build there is no third UI thread and no marshalling layer
here, because `MacStatusItemShell` already hands every AppKit mutation to the
main queue itself. The controller can therefore be driven straight from the
worker.

Bluetooth pairing and bond removal remain owned by macOS.
"""

from __future__ import annotations

import asyncio
import concurrent.futures
import logging
import os
import sys
import threading
import time
import webbrowser
from collections.abc import Callable

from quotaframe_bridge import __version__
from quotaframe_bridge.i18n import tr
from quotaframe_bridge.cli.instance_lock import (
    BridgeAlreadyRunningError,
    BridgeInstanceLock,
)
from quotaframe_bridge.macos_bluetooth import BluetoothPermissionError, preflight
from quotaframe_bridge.pairing.darwin import DarwinPairer
from quotaframe_bridge.release_config import release_repository
from quotaframe_bridge.service.firmware_update import FirmwareUpdateMonitor
from quotaframe_bridge.sources.firmware_release import FirmwareReleaseSource
from quotaframe_bridge.ui.worker_cleanup import cleanup_worker_tasks
from quotaframe_bridge.ui.firmware_actions import FirmwareUpdateActions
from quotaframe_bridge.service.multi_device import MultiDeviceBridgeService
from quotaframe_bridge.service.update import (
    BridgeUpdateMonitor,
    UpdateState,
)
from quotaframe_bridge.sources.product_release import ProductReleaseSource
from quotaframe_bridge.sources.selection import create_usage_source
from quotaframe_bridge.ui import (
    TrayServiceGraph,
    build_tray_graph,
    run_adoption,
    snapshot_tray_status,
)
from quotaframe_bridge.ui.logging_setup import configure_logging, log_path
from quotaframe_bridge.ui.update_presenter import (
    BrowserOpen,
    UpdatePresenter,
)
from quotaframe_bridge.ui.controller import TrayController
from quotaframe_bridge.ui.macos.autostart import disable, enable, is_enabled
from quotaframe_bridge.ui.macos.dialogs import MacDialogs, open_log_directory
from quotaframe_bridge.ui.macos.status_item import MacStatusItemShell
from quotaframe_bridge.ui.notifications import NotificationPolicy
from quotaframe_bridge.versioning import SemVer

LOGGER = logging.getLogger(__name__)

STATUS_INTERVAL_S = 5.0
RESTART_DELAY_S = 30.0
AUTOSTART_FLAG = "--autostarted"

MonitorFactory = Callable[..., BridgeUpdateMonitor]


class MenuBarApplication(FirmwareUpdateActions):
    """Own the two threads and the wiring between them."""

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
        self._dialogs = MacDialogs()
        self._policy = NotificationPolicy()
        self._loop: asyncio.AbstractEventLoop | None = None
        self._service: MultiDeviceBridgeService | None = None
        self._graph: TrayServiceGraph[DarwinPairer] | None = None
        self._adoption_running = False
        self._stopping = False
        self._shutdown_lock = threading.Lock()
        self._firmware_updates = 0
        self._firmware_update_monitor = FirmwareUpdateMonitor(
            FirmwareReleaseSource(), self._firmware_manifest_url
        )
        self._log_path = log_path()
        self._shell = MacStatusItemShell(
            on_toggle_screensaver=self._toggle_screensaver,
            on_turn_page=self._turn_page,
            on_refresh=self._refresh,
            on_add_device=self._add_device,
            on_repair=self._repair,
            on_forget_device=self._forget_device,
            on_toggle_autostart=self._toggle_autostart,
            on_open_log=self._open_log,
            on_quit=self._quit,
            on_bridge_update=self._bridge_update,
            on_firmware_update=self._firmware_update,
        )
        self._controller = TrayController(self._shell, self._policy)
        self._update_presenter = UpdatePresenter(
            self._controller,
            webbrowser.open if browser is None else browser,
        )
        make_monitor = (
            BridgeUpdateMonitor if monitor_factory is None else monitor_factory
        )
        self._update_monitor = make_monitor(
            product_source,
            SemVer.parse(__version__),
            self._update_presenter,
        )
        self._controller.set_autostart_checked(is_enabled())

    # -- menu handlers, main thread ---------------------------------------

    def _turn_page(self, direction: int) -> None:
        loop, service = self._loop, self._service
        if loop is not None and service is not None and not self._stopping:
            loop.call_soon_threadsafe(service.turn_pages, direction)

    def _toggle_screensaver(self) -> None:
        loop, service = self._loop, self._service
        if loop is not None and service is not None and not self._stopping:
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

    async def _confirm_firmware(self, device, status, image) -> bool:
        loop = asyncio.get_running_loop()
        decision = loop.create_future()
        def show():
            if decision.done():
                return
            result = self._dialogs.confirm_firmware(self._firmware_confirmation_text(device, status, image))
            def deliver():
                if not decision.done():
                    decision.set_result(result)
            loop.call_soon_threadsafe(deliver)
        self._shell.dispatch(show)
        return await decision

    def _set_firmware_action(self, label: str, enabled: bool, detail: str) -> None:
        graph = self._graph
        device = None if graph is None else graph.get(label)
        self._shell.dispatch(lambda: self._controller.set_firmware_action(label, enabled, detail)
                             if graph is not None and self._graph is graph and graph.get(label) is device else None)

    def _notify_firmware(self, title: str, body: str) -> None:
        self._controller.notify(title, body)

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

    def _repair(self) -> None:
        """Explain the manual steps; nothing here can remove a bond."""

        self._dialogs.show_repair_guidance()

    def _forget_device(self) -> None:
        """Ask which adopted board to drop, then drop it on the worker loop.

        Only the Bridge's own record goes away. The macOS bond survives,
        because CoreBluetooth exposes no way to remove one — the same reason
        `_repair` can only show instructions.
        """

        loop, graph = self._loop, self._graph
        if loop is None or graph is None:
            return
        choices = graph.devices
        chosen = self._dialogs.choose_device_to_forget(tuple(device.name for device in choices))
        if chosen is None:
            return
        device = choices[chosen]
        address = device.address
        LOGGER.info("forget device requested: device=%s", chosen)
        asyncio.run_coroutine_threadsafe(
            self._forget_async(graph, address, expected=device), loop
        )

    async def _forget_async(
        self, graph: TrayServiceGraph[DarwinPairer], address: str, *, expected
    ) -> None:
        try:
            await graph.forget(address, expected=expected)
        except Exception:
            LOGGER.exception("could not remove device: address=%s", address)

    def _toggle_autostart(self, checked: bool) -> None:
        try:
            if checked:
                enable()
            else:
                disable()
        except OSError:
            LOGGER.exception("could not update the login item")
        self._controller.set_autostart_checked(is_enabled())

    def _open_log(self) -> None:
        open_log_directory(self._log_path.parent)

    def _quit(self) -> None:
        with self._shutdown_lock:
            self._stopping = True
        loop = self._loop
        if loop is not None:
            loop.call_soon_threadsafe(loop.stop)
        self._controller.stop()
        self._terminate()

    def _terminate(self) -> None:
        import AppKit

        AppKit.NSApplication.sharedApplication().terminate_(None)

    # -- worker thread -----------------------------------------------------

    def _build_service(self) -> MultiDeviceBridgeService:
        graph = build_tray_graph(
            usage_source_factory=lambda: create_usage_source(None),
            # DarwinPairer delegates pairing dialogs to macOS and therefore
            # does not require a cross-device ceremony lock.
            pairer_factory=DarwinPairer,
        )
        self._graph = graph
        return graph.service

    async def _run_adoption(
        self, graph: TrayServiceGraph[DarwinPairer]
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
        self._controller.notify(tr("device_added"), tr("device_added_body", name=name))

    async def _publish_status(self, graph: TrayServiceGraph[DarwinPairer]) -> None:
        while True:
            # Safe from this thread: every shell mutation marshals itself.
            self._controller.apply(
                snapshot_tray_status(
                    graph, adoption_running=self._adoption_running
                ),
                time.monotonic(),
            )
            self._shell.set_devices(tuple((device.address, device.name, device.session) for device in graph.devices))
            self._publish_firmware_actions(graph)
            await asyncio.sleep(STATUS_INTERVAL_S)

    async def _monitor_updates(self) -> None:
        try:
            await self._update_monitor.run()
        except asyncio.CancelledError:
            raise
        except Exception as exc:
            LOGGER.warning(
                "automatic update monitor stopped: %s", type(exc).__name__
            )

    async def _run_background_tasks(
        self,
        service: MultiDeviceBridgeService,
        graph: TrayServiceGraph[DarwinPairer],
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
        import AppKit

        threading.Thread(target=self._worker, daemon=False).start()
        application = AppKit.NSApplication.sharedApplication()
        # A status-bar-only process: no dock icon, no application menu.
        application.setActivationPolicy_(
            AppKit.NSApplicationActivationPolicyAccessory
        )
        application.run()


def main(argv: list[str] | None = None, *, platform: str | None = None) -> int:
    """Entry point. Never prints usage: there is no console to see it."""

    arguments = sys.argv[1:] if argv is None else argv
    if "--version" in arguments:
        print(f"QuotaFrame Bridge {__version__}")
        return 0
    if (sys.platform if platform is None else platform) != "darwin":
        # Installed as a console script on every platform, because project
        # scripts take no environment markers. Saying so beats an AppKit
        # ImportError traceback.
        print(
            "the menu bar application is macOS only; "
            "on Windows run quotaframe-bridge.exe",
            file=sys.stderr,
        )
        return 1
    unknown = [item for item in arguments if item != AUTOSTART_FLAG]
    configure_logging()
    if unknown:
        LOGGER.warning("ignoring unrecognised arguments: %s", " ".join(unknown))
    try:
        preflight()
    except BluetoothPermissionError as exc:
        LOGGER.error("%s", exc)
        return 1
    try:
        with BridgeInstanceLock.acquire():
            MenuBarApplication().run()
    except BridgeAlreadyRunningError:
        LOGGER.error("another instance already holds the lock")
        MacDialogs().show_already_running()
        return 1
    except Exception:
        LOGGER.exception("menu bar application failed to start")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
