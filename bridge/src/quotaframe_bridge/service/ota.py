"""Testable Folder Push OTA transfer orchestration."""

from __future__ import annotations

import asyncio
import hashlib
import logging
import time
import struct
from pathlib import Path
from collections.abc import Awaitable, Callable
from dataclasses import dataclass
from typing import Protocol

from quotaframe_bridge.protocol.messages import CommandRejected, DeviceStatus, Sequence
from quotaframe_bridge.protocol.ota_messages import (
    FOLDER_PUSH_BLOCK_BYTES,
    FOLDER_PUSH_FIRMWARE_PATH,
    FOLDER_PUSH_MANIFEST_PATH,
    encode_folder_push_begin,
    encode_folder_push_chunk,
    encode_folder_push_end,
    encode_folder_push_file,
    encode_folder_push_file_end,
    encode_ota_abort,
    encode_ota_manifest,
)
from quotaframe_bridge.sources.firmware_release import FirmwareImage
from quotaframe_bridge.targets import TARGETS_BY_ID
from quotaframe_bridge.transports.base import WritePolicy
from quotaframe_bridge.versioning import SemVer, VersionError


logger = logging.getLogger(__name__)


class UpgradeError(RuntimeError):
    """A sanitized OTA preflight, transfer, or verification failure."""


@dataclass(frozen=True, slots=True)
class UpgradeResult:
    """Successful OTA result returned after the updated device reconnects."""

    success: bool
    version: str


class OtaTransport(Protocol):
    """Transport capabilities required by the Folder Push OTA orchestrator."""

    async def query_status(self) -> DeviceStatus: ...
    async def send_command(
        self,
        payload: bytes,
        command: str,
        expected_ack_n: int,
        write_policy: WritePolicy = WritePolicy.NORMAL,
    ) -> None: ...
    async def connect(self) -> None: ...


ProgressCallback = Callable[[tuple[int, int]], None]
Sleep = Callable[[float], Awaitable[None]]


class OtaService:
    """Run one validated, user-confirmed Folder Push firmware upgrade.

    The Bridge first validates the release locally and confirms the connected
    target/version. It then resets stale OTA state, sends the manifest, waits
    for physical confirmation on the panel, streams firmware in bounded blocks,
    and finally waits for the device to reboot with the expected version.
    Transfer failures snapshot the device's retained error, then trigger a
    best-effort abort so the device does not retain an abandoned OTA session.
    """

    def __init__(
        self,
        *,
        upgrade_timeout: float = 600.0,
        cleanup_timeout: float = 5.0,
        confirm_timeout: float = 60.0,
        reboot_timeout: float = 60.0,
        poll_interval: float = 0.5,
        sleep: Sleep = asyncio.sleep,
    ) -> None:
        if confirm_timeout < 0 or reboot_timeout < 0 or poll_interval <= 0:
            raise ValueError("OTA timeouts must be non-negative and polling positive")
        if upgrade_timeout <= 0 or cleanup_timeout < 0:
            raise ValueError("OTA upgrade timeout must be positive and cleanup non-negative")
        self.upgrade_timeout = upgrade_timeout
        self.cleanup_timeout = cleanup_timeout
        self.confirm_timeout = confirm_timeout
        self.reboot_timeout = reboot_timeout
        self.poll_interval = poll_interval
        self.sleep = sleep

    async def upgrade(
        self,
        transport: OtaTransport,
        image: FirmwareImage,
        payload: bytes,
        progress: ProgressCallback | None = None,
        expected_status: DeviceStatus | None = None,
    ) -> UpgradeResult:
        """Upgrade to `image`, returning only after the new version reconnects.

        `payload` must exactly match the manifest size and SHA-256 before any
        device command is sent. Progress callbacks report firmware bytes only;
        manifest bytes are intentionally excluded from the user-facing meter.
        """

        self._validate_payload(image, payload)
        deadline = asyncio.get_running_loop().time() + self.upgrade_timeout
        try:
            async with asyncio.timeout_at(deadline):
                initial = await self._status(transport, "device preflight failed")
                self._require_compatible(initial, image)
                if expected_status is not None and (
                    initial.firmware_project, initial.target, SemVer.from_device(initial.firmware_version),
                    initial.capabilities
                ) != (
                    expected_status.firmware_project, expected_status.target,
                    SemVer.from_device(expected_status.firmware_version), expected_status.capabilities
                ):
                    raise UpgradeError("device changed after firmware confirmation")
        except TimeoutError:
            raise UpgradeError("device preflight timed out") from None
        try:
            async with asyncio.timeout_at(deadline):
                return await self._upgrade(transport, image, payload, progress)
        except (Exception, asyncio.CancelledError) as exc:
            logger.warning(
                "firmware update interrupted: target=%s version=%s error=%s",
                image.target, image.version, type(exc).__name__, exc_info=True,
            )
            retained = None
            try:
                async with asyncio.timeout(self.cleanup_timeout):
                    if getattr(transport, "connected", False):
                        retained = await self._retained_device_error(transport)
                        if getattr(transport, "connected", False):
                            await self._best_effort_abort(transport, 1)
            except asyncio.CancelledError:
                raise
            except Exception:
                pass
            if isinstance(exc, asyncio.CancelledError):
                raise
            if retained is not None:
                logger.warning(
                    "firmware device failure: target=%s reason=%s", image.target, retained
                )
                raise UpgradeError(retained) from exc
            if isinstance(exc, TimeoutError):
                raise UpgradeError("firmware update timed out") from exc
            raise

    async def _upgrade(
        self, transport: OtaTransport, image: FirmwareImage, payload: bytes,
        progress: ProgressCallback | None,
    ) -> UpgradeResult:
        sequence = Sequence()
        abort_sequence = sequence.next()
        await self._send(
            transport,
            encode_ota_abort(abort_sequence),
            "ota_abort",
            abort_sequence,
        )
        manifest = encode_ota_manifest(
            image.target, image.size, image.sha256, image.version, image.firmware_project
        )
        await self._send(
            transport,
            encode_folder_push_begin(len(manifest) + len(payload)),
            "char_begin",
            0,
        )
        await self._send_file(
            transport, FOLDER_PUSH_MANIFEST_PATH, manifest
        )
        await self._wait_for_confirmation(transport)
        transfer_started = time.monotonic()
        await self._send_file(
            transport, FOLDER_PUSH_FIRMWARE_PATH, payload, progress
        )
        transfer_seconds = time.monotonic() - transfer_started
        throughput = (
            len(payload) / 1024 / transfer_seconds
            if transfer_seconds > 0
            else 0.0
        )
        logger.info(
            "firmware transfer complete: target=%s version=%s "
            "bytes=%d seconds=%.3f kib_per_second=%.2f",
            image.target,
            image.version,
            len(payload),
            transfer_seconds,
            throughput,
        )
        verification_started = time.monotonic()
        await self._send(
            transport, encode_folder_push_end(), "char_end", 0
        )
        logger.info(
            "firmware verification complete: target=%s version=%s "
            "seconds=%.3f",
            image.target,
            image.version,
            time.monotonic() - verification_started,
        )

        reboot_started = time.monotonic()
        await self._wait_for_reboot(transport, image)
        logger.info(
            "updated device returned: target=%s version=%s seconds=%.3f",
            image.target,
            image.version,
            time.monotonic() - reboot_started,
        )
        return UpgradeResult(success=True, version=image.version)

    async def _send_file(
        self,
        transport: OtaTransport,
        path: str,
        content: bytes,
        progress: ProgressCallback | None = None,
    ) -> None:
        await self._send(
            transport, encode_folder_push_file(path, len(content)), "file", 0
        )
        offset = 0
        while offset < len(content):
            block = content[offset : offset + FOLDER_PUSH_BLOCK_BYTES]
            offset += len(block)
            await self._send(
                transport,
                encode_folder_push_chunk(block),
                "chunk",
                offset,
                WritePolicy.FOLDER_PUSH,
            )
            if progress is not None:
                progress((offset, len(content)))
        await self._send(
            transport, encode_folder_push_file_end(), "file_end", len(content)
        )

    @staticmethod
    def _validate_payload(image: FirmwareImage, payload: bytes) -> None:
        if not isinstance(payload, bytes) or len(payload) != image.size:
            raise UpgradeError("firmware image size validation failed")
        if hashlib.sha256(payload).hexdigest() != image.sha256:
            raise UpgradeError("firmware image digest validation failed")
        try:
            manifest = encode_ota_manifest(image.target, image.size, image.sha256, image.version, image.firmware_project)
        except ValueError as exc:
            raise UpgradeError("firmware manifest is invalid") from exc
        if image.firmware_project != "quotaframe":
            raise UpgradeError("firmware project is not supported")
        target = TARGETS_BY_ID.get(image.target)
        if target is None:
            raise UpgradeError("firmware target is not maintained")
        if (len(payload) < 288 or payload[0] != 0xE9
                or struct.unpack_from("<H", payload, 12)[0] != target.image_chip_id
                or struct.unpack_from("<I", payload, 32)[0] != 0xABCD5432):
            raise UpgradeError("firmware application header is invalid")
        for offset, expected in ((48, image.version), (80, Path(target.ota_image).stem)):
            if payload[offset:offset + 32].split(b"\0", 1)[0] != expected.encode("ascii"):
                raise UpgradeError("firmware application identity is invalid")
        if (
            len(payload) > target.ota_partition_bytes
            or len(manifest) + len(payload) > target.ota_transfer_bytes
        ):
            raise UpgradeError("firmware image exceeds the OTA transfer limit")

    @staticmethod
    def _require_compatible(status: DeviceStatus, image: FirmwareImage) -> None:
        if "ota.folder.v1" not in status.capabilities or status.ota is None:
            raise UpgradeError("connected device does not support firmware updates")
        if not status.secure or status.firmware_project != image.firmware_project:
            raise UpgradeError("firmware project does not match connected device")
        if status.target != image.target:
            raise UpgradeError("firmware target does not match connected device")
        try:
            is_newer = SemVer.parse(image.version) > SemVer.from_device(
                status.firmware_version
            )
        except VersionError:
            raise UpgradeError("firmware version validation failed") from None
        if not is_newer:
            raise UpgradeError("release firmware is not newer than the device")
        if status.ota.phase != "idle":
            raise UpgradeError("device already has an active firmware update")

    async def _wait_for_confirmation(self, transport: OtaTransport) -> None:
        try:
            async with asyncio.timeout(self.confirm_timeout):
                while True:
                    current = await self._status(transport, "firmware confirmation link lost")
                    if current.ota is None:
                        raise UpgradeError("device stopped reporting firmware update status")
                    if current.ota.phase == "receiving":
                        return
                    if current.ota.phase == "idle" and current.ota.error:
                        raise UpgradeError(self._public_error(current.ota.error))
                    if current.ota.phase != "confirming":
                        raise UpgradeError("device entered an unexpected firmware update state")
                    await self.sleep(self.poll_interval)
        except TimeoutError:
            raise UpgradeError("firmware confirmation timed out") from None

    async def _wait_for_reboot(
        self, transport: OtaTransport, image: FirmwareImage
    ) -> None:
        if self.reboot_timeout == 0:
            raise UpgradeError("updated device did not reconnect in time")
        try:
            async with asyncio.timeout(self.reboot_timeout):
                while True:
                    try:
                        current = await transport.query_status()
                    except Exception:
                        await transport.connect()
                    else:
                        if current.ota is not None and current.ota.error:
                            raise UpgradeError(self._public_error(current.ota.error))
                        if (current.firmware_version.removeprefix("v") == image.version and current.boot_valid
                                and current.firmware_project == image.firmware_project
                                and current.target == image.target):
                            return
                    await self.sleep(self.poll_interval)
        except TimeoutError:
            raise UpgradeError("updated device did not confirm startup in time") from None

    @staticmethod
    async def _status(transport: OtaTransport, public_error: str) -> DeviceStatus:
        try:
            return await transport.query_status()
        except Exception:
            raise UpgradeError(public_error) from None

    @staticmethod
    async def _retained_device_error(transport: OtaTransport) -> str | None:
        """Snapshot the device's structured OTA error before abort wipes it.

        Transfer commands fail before any status poll can observe the reset,
        so at this moment device status holds the only record of why the
        transfer was refused (e.g. the power gate's low_power).
        """
        try:
            current = await transport.query_status()
        except Exception:
            return None
        if current.ota is None or not current.ota.error:
            return None
        return OtaService._public_error(current.ota.error)

    @staticmethod
    async def _send(
        transport: OtaTransport,
        payload: bytes,
        command: str,
        expected_ack_n: int,
        write_policy: WritePolicy = WritePolicy.NORMAL,
    ) -> None:
        try:
            await transport.send_command(
                payload, command, expected_ack_n, write_policy
            )
        except CommandRejected:
            logger.warning(
                "firmware command rejected: command=%s expected_ack=%d",
                command, expected_ack_n,
            )
            raise UpgradeError(f"device rejected {command}") from None
        except Exception:
            logger.warning(
                "firmware command failed: command=%s expected_ack=%d",
                command, expected_ack_n, exc_info=True,
            )
            raise UpgradeError(f"connection lost during {command}; command outcome is unknown") from None

    @classmethod
    async def _best_effort_abort(
        cls, transport: OtaTransport, sequence: int
    ) -> None:
        try:
            await cls._send(
                transport,
                encode_ota_abort(sequence),
                "ota_abort",
                sequence,
            )
        except UpgradeError:
            pass

    @staticmethod
    def _public_error(error: str) -> str:
        return {
            "project_mismatch": "firmware project does not match the device",
            "target_mismatch": "firmware target does not match the device",
            "denied": "firmware update was denied on the device",
            "timeout": "firmware confirmation timed out",
            "too_large": "firmware image does not fit the device",
            "bad_image": "device rejected the firmware image",
            "link_lost": "firmware transfer link was lost",
            "seq_gap": "firmware transfer sequence was interrupted",
            "low_power": "device battery too low - plug in or charge before updating",
        }.get(error, "firmware update failed")
