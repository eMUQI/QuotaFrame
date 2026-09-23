"""Shared firmware-update workflow for desktop application entry points."""

from __future__ import annotations

import asyncio
import concurrent.futures
import logging
import os

from quotaframe_bridge.i18n import tr
from quotaframe_bridge.service.ota import UpgradeError
from quotaframe_bridge.service.firmware_install import install_firmware
from quotaframe_bridge.sources.firmware_release import catalog_url
from quotaframe_bridge.sources.firmware_release import FirmwareReleaseError
from quotaframe_bridge.release_config import ReleaseConfigError
from quotaframe_bridge.service.devices import TrayServiceGraph

LOGGER = logging.getLogger(__name__)
FIRMWARE_MANIFEST_ENV = "QUOTAFRAME_FIRMWARE_MANIFEST_URL"


class FirmwareUpdateActions:
    """Share OTA orchestration while each shell owns UI-thread dispatch.

    Applications supply the service graph, firmware monitor, event loop, and
    shutdown lock. UI callbacks dispatch through
    the platform shell before accessing native controls.
    """

    def _firmware_update(self, address: str) -> None:
        loop = self._loop
        if loop is None:
            return
        graph = self._graph
        device = None if graph is None else graph.get(address)
        if device is None:
            return
        future = asyncio.run_coroutine_threadsafe(
            self._firmware_update_async(address, expected=device), loop
        )

        def report_failure(completed: concurrent.futures.Future[object]) -> None:
            try:
                completed.result()
            except concurrent.futures.CancelledError:
                pass
            except Exception:
                LOGGER.exception("firmware update task failed: device=%s", address)

        future.add_done_callback(report_failure)

    async def _firmware_update_async(self, address: str, *, expected) -> None:
        if self._graph is None or self._graph.get(address) is not expected:
            return
        task = getattr(expected, "update_task", None)
        if task is not None:
            if getattr(expected, "update_cancellable", False) and not task.cancelling():
                expected.update_cancel_requested = True
                expected.update_cancellable = False
                self._set_firmware_action(address, False, tr("firmware_cancelling"))
                task.cancel()
            return
        with self._shutdown_lock:
            if self._stopping:
                return
            self._firmware_updates += 1
        try:
            await self._run_firmware_update(address)
        finally:
            with self._shutdown_lock:
                self._firmware_updates -= 1

    async def _run_firmware_update(self, address: str) -> None:
        graph = self._graph
        device = None if graph is None else graph.get(address)
        label = address if device is None else device.name
        session = None if device is None else device.session
        manager = None if device is None else device.manager
        manifest_url = self._firmware_manifest_url()
        if session is None or manager is None:
            self._notify_firmware(tr("firmware_failed"), tr("device_unavailable", label=label))
            return
        if not manifest_url:
            self._notify_firmware(
                tr("firmware_unavailable"), tr("firmware_not_configured")
            )
            return
        if getattr(session, "exclusive_active", False) or device.update_task is not None:
            self._notify_firmware(tr("firmware_failed"), tr("updating"))
            return
        device_status = manager.device_status
        if (
            device_status is None
            or "ota.folder.v1" not in device_status.capabilities
            or not device_status.target
        ):
            self._notify_firmware(
                tr("firmware_unavailable"), tr("firmware_not_supported", label=label)
            )
            return

        LOGGER.info(
            "firmware update preparing: device=%s target=%s current=%s",
            label,
            device_status.target,
            device_status.firmware_version,
        )
        self._set_firmware_action(address, True, tr("firmware_cancel_preparing"))
        self._notify_firmware(
            tr("firmware_preparing", label=label),
            tr("firmware_preparing_body"),
        )
        notification = None
        try:
            def progress(value: tuple[int, int]) -> None:
                offset, size = value
                percent = min(100, (offset * 100) // size) if size else 0
                self._set_firmware_action(
                    address, percent < 100,
                    tr("firmware_cancel_progress", percent=percent) if percent < 100
                    else tr("firmware_verifying"),
                )

            result = await install_firmware(
                graph, device, manifest_url, self._confirm_firmware, progress)
        except asyncio.CancelledError:
            if getattr(device, "update_cancel_requested", False) and graph.get(address) is device:
                self._notify_firmware(
                    tr("firmware_cancelled"), tr("firmware_cancelled_body", label=label)
                )
            raise
        except TimeoutError:
            notification = (tr("firmware_failed"), tr("firmware_download_timeout"))
        except (FirmwareReleaseError, UpgradeError, RuntimeError) as exc:
            notification = (tr("firmware_failed"), f"{label}：{exc}")
        except Exception:
            notification = (tr("firmware_failed"), tr("firmware_interrupted", label=label))
        else:
            if result is not None:
                notification = (
                    tr("firmware_complete"),
                    tr("firmware_complete_body", label=label, version=result.version),
                )
        finally:
            if graph.get(address) is device:
                self._publish_firmware_actions(graph)
        if notification is not None and graph.get(address) is device:
            self._notify_firmware(*notification)

    def _firmware_manifest_url(self) -> str | None:
        override = os.environ.get(FIRMWARE_MANIFEST_ENV, "").strip()
        if override:
            return override
        try:
            return catalog_url()
        except ReleaseConfigError:
            return None

    def _publish_firmware_actions(
        self, graph: TrayServiceGraph
    ) -> None:
        manifest_configured = bool(self._firmware_manifest_url())
        for device in graph.devices:
            label = device.name
            manager = device.manager
            session = device.session
            device_status = manager.device_status
            capable = (
                manager.connected
                and device_status is not None
                and "ota.folder.v1" in device_status.capabilities
            )
            notification = None
            if device.update_task is not None:
                continue
            if session.exclusive_active:
                enabled, detail = False, tr("updating")
            elif not manifest_configured:
                enabled, detail = False, tr("not_configured")
            elif capable:
                presentation = self._firmware_update_monitor.presentation(
                    label,
                    device_status.target,
                    device_status.firmware_version,
                    device_status.firmware_project,
                    session,
                )
                enabled, detail = presentation.enabled, presentation.detail
                notification = presentation.notification
            else:
                enabled, detail = False, tr("unavailable")
            self._set_firmware_action(device.address, enabled, detail)
            if notification is not None:
                self._notify_firmware(*notification)

    async def _monitor_firmware_updates(self) -> None:
        try:
            await self._firmware_update_monitor.run()
        except asyncio.CancelledError:
            raise
        except Exception as exc:
            LOGGER.warning(
                "automatic firmware update monitor stopped: %s",
                type(exc).__name__,
            )

    @staticmethod
    def _firmware_confirmation_text(device, status, image):
        development = bool(os.environ.get(FIRMWARE_MANIFEST_ENV)
                           or os.environ.get("QUOTAFRAME_RELEASE_REPO"))
        source = tr("firmware_development_source" if development else "firmware_official_source")
        return tr("firmware_confirmation", name=device.name, address=device.address, source=source,
                  url=image.url, project=image.firmware_project, target=image.target,
                  current=status.firmware_version, version=image.version,
                  size=image.size, sha256=image.sha256)

    async def _confirm_firmware(self, device, status, image) -> bool:
        raise NotImplementedError

    def _set_firmware_action(self, label: str, enabled: bool, detail: str) -> None:
        raise NotImplementedError

    def _notify_firmware(self, title: str, body: str) -> None:
        raise NotImplementedError
