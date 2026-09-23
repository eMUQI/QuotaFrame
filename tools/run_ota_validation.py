"""Push one local public firmware image over an existing Windows BLE bond.

This is a validation tool, not a production pairing path. The target must
already be bonded securely; `_BondedPairer` intentionally skips the normal
WinRT pairing ceremony so a scripted OTA validation does not mutate pairing
state while measuring the transfer/reboot path.
"""

from __future__ import annotations

import argparse
import asyncio
import hashlib
import time
from pathlib import Path

from quotaframe_bridge.service.ota import OtaService
from quotaframe_bridge.sources.firmware_release import FirmwareImage
from quotaframe_bridge.transports.base import WritePolicy
from quotaframe_bridge.transports.bleak_nus import BleakNusTransport


class _BondedPairer:
    """No-op pairer used only after the operator has established a valid bond."""

    async def ensure_paired(self, _device: object) -> None:
        return None


class _ReportingTransport:
    """Forward OTA transport calls while emitting operator-facing checkpoints."""

    def __init__(self, inner: BleakNusTransport) -> None:
        self.inner = inner

    async def connect(self) -> None:
        await self.inner.connect()

    async def query_status(self):
        return await self.inner.query_status()

    @property
    def connected(self) -> bool:
        return self.inner.connected

    async def send_command(
        self,
        payload: bytes,
        command: str,
        expected_ack_n: int,
        write_policy: WritePolicy = WritePolicy.NORMAL,
    ) -> None:
        await self.inner.send_command(payload, command, expected_ack_n, write_policy)
        if command == "file_end" and self.inner.status is not None:
            status = await self.inner.query_status()
            if status.ota is not None and status.ota.phase == "confirming":
                print("CONFIRM_NOW", flush=True)


async def _run(args: argparse.Namespace) -> int:
    payload = args.image.read_bytes()
    digest = hashlib.sha256(payload).hexdigest()
    image = FirmwareImage(
        target=args.target,
        firmware_project="quotaframe",
        version=args.version,
        size=len(payload),
        sha256=digest,
        url=(
            "https://github.com/validation/local/releases/download/"
            f"v{args.version}/{args.image.name}"
        ),
    )
    inner = BleakNusTransport(
        pairer=_BondedPairer(),
        name_prefix=args.name_prefix,
        scan_timeout=15,
        connect_timeout=30,
        ack_timeout=10,
    )
    transport = _ReportingTransport(inner)
    last_decile = -1
    started = time.perf_counter()

    def progress(value: tuple[int, int]) -> None:
        nonlocal last_decile
        offset, size = value
        percent = (offset * 100) // size
        decile = percent // 10
        if decile != last_decile or percent == 100:
            last_decile = decile
            print(f"PROGRESS {percent}% {offset}/{size}", flush=True)

    try:
        await inner.connect()
        status = inner.status
        if status is None or status.ota is None:
            raise RuntimeError("device did not return OTA status")
        print(
            f"CONNECTED fw={status.firmware_version} "
            f"project={status.firmware_project} target={status.target} phase={status.ota.phase}",
            flush=True,
        )
        result = await OtaService(
            confirm_timeout=60, reboot_timeout=60
        ).upgrade(transport, image, payload, progress)
        print(
            f"OTA_SUCCESS version={result.version} "
            f"elapsed={time.perf_counter() - started:.2f}s sha256={digest}",
            flush=True,
        )
        return 0
    except Exception as exc:
        print(f"OTA_FAILED {type(exc).__name__}: {exc}", flush=True)
        return 1
    finally:
        await inner.close()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--target", required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--name-prefix", required=True)
    return asyncio.run(_run(parser.parse_args()))


if __name__ == "__main__":
    raise SystemExit(main())
