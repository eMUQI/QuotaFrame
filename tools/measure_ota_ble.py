"""Measure complete JSON-line delivery over a bonded usage-panel BLE link.

This development tool sends deterministic public padding only. It never prints
device addresses, raw notifications, bond material, or account data.
"""

from __future__ import annotations

import argparse
import asyncio
import base64
import json
import math
import statistics
import time
from contextlib import nullcontext
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

NUS_SERVICE_UUID = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
NUS_RX_UUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
NUS_TX_UUID = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"
LINE_LIMIT = 4096
PAYLOAD_BYTES = 2880
DEFAULT_TIMEOUT_SECONDS = 10.0


@dataclass(frozen=True, slots=True)
class ProbeResult:
    """One complete-line measurement using public deterministic bytes."""

    payload_bytes: int
    elapsed_seconds: float

    @property
    def kib_per_second(self) -> float:
        return self.payload_bytes / 1024 / self.elapsed_seconds


def build_probe_line(seq: int, payload_bytes: int = PAYLOAD_BYTES) -> bytes:
    """Build one valid, deterministic, privacy-safe JSON probe command."""

    if isinstance(seq, bool) or not 0 <= seq <= 0xFFFFFFFF:
        raise ValueError("seq must fit unsigned 32-bit")
    if isinstance(payload_bytes, bool) or not 1 <= payload_bytes <= PAYLOAD_BYTES:
        raise ValueError(f"payload_bytes must be between 1 and {PAYLOAD_BYTES}")
    raw = bytes(index % 251 for index in range(payload_bytes))
    line = (
        json.dumps(
            {
                "b64": base64.b64encode(raw).decode("ascii"),
                "cmd": f"ota_probe_{seq:08x}",
                "seq": str(seq),
                "v": "1",
            },
            ensure_ascii=True,
            allow_nan=False,
            separators=(",", ":"),
            sort_keys=True,
        )
        + "\n"
    ).encode("ascii")
    if len(line) > LINE_LIMIT:
        raise ValueError("probe line exceeds Buddy line limit")
    return line


class LineInbox:
    """Collect newline-delimited JSON notifications from one connection."""

    def __init__(self) -> None:
        self._buffer = bytearray()
        self._queue: asyncio.Queue[dict[str, Any]] = asyncio.Queue()

    def feed(self, data: bytearray | bytes) -> None:
        self._buffer.extend(data)
        if len(self._buffer) > LINE_LIMIT * 2:
            self._buffer.clear()
            self._queue.put_nowait({"ack": "protocol_error"})
            return
        while True:
            newline = self._buffer.find(b"\n")
            if newline < 0:
                return
            raw = bytes(self._buffer[:newline])
            del self._buffer[: newline + 1]
            try:
                message = json.loads(raw)
            except (UnicodeDecodeError, json.JSONDecodeError):
                continue
            if isinstance(message, dict):
                self._queue.put_nowait(message)

    async def receive_reply(self, command: str, timeout: float) -> dict[str, Any]:
        """Ignore unrelated replies within one fixed deadline."""
        async with asyncio.timeout(timeout):
            while True:
                reply = await self._queue.get()
                if reply.get("ack") == "protocol_error":
                    raise RuntimeError("notification line exceeded the receive limit")
                if reply.get("ack") == command:
                    return reply


def validate_probe_reply(reply: dict[str, Any], command: str) -> None:
    """Unknown-command ACKs use n=0; the unique command correlates the sample."""
    if (reply.get("ack") != command or reply.get("ok") is not False
            or type(reply.get("n")) is not int or reply["n"] != 0
            or reply.get("error") != "unknown_command"):
        raise RuntimeError("unexpected probe acknowledgement")


def summarize_samples(samples: list[dict[str, Any]], elapsed_s: float | None = None) -> dict[str, Any]:
    measured = [sample for sample in samples if not sample["warmup"]]
    good = [sample for sample in measured if sample["success"]]
    duration = elapsed_s if elapsed_s is not None else sum(sample["elapsed_s"] for sample in measured)
    latencies = sorted(sample["elapsed_s"] for sample in good)
    rates = [ProbeResult(sample["payload_bytes"], sample["elapsed_s"]).kib_per_second
             for sample in good]
    return {
        "type": "summary", "attempted": len(measured), "succeeded": len(good),
        "failed": len(measured) - len(good), "elapsed_s": duration,
        "goodput_kib_s": (sum(sample["payload_bytes"] for sample in good)
                          / 1024 / duration if duration else None),
        "wire_kib_s": (sum(sample.get("line_bytes", 0) for sample in good)
                       / 1024 / duration if duration else None),
        "success_median_kib_s": statistics.median(rates) if rates else None,
        "success_rtt_median_s": statistics.median(latencies) if latencies else None,
        "success_rtt_p95_s": latencies[math.ceil(len(latencies) * .95) - 1] if latencies else None,
        "success_rtt_max_s": max(latencies) if latencies else None,
    }


def _write_size(client: Any, characteristic: Any, cap: int, response: bool) -> int:
    limits = [cap]
    mtu = getattr(client, "mtu_size", None)
    if isinstance(mtu, int) and mtu > 3:
        limits.append(mtu - 3)
    if not response:
        reported = getattr(characteristic, "max_write_without_response_size", None)
        if isinstance(reported, int) and reported > 0:
            limits.append(reported)
    return min(limits)


async def _write_line(
    client: Any,
    characteristic: Any,
    line: bytes,
    *,
    cap: int,
    response: bool,
) -> int:
    size = _write_size(client, characteristic, cap, response)
    chunks = 0
    for offset in range(0, len(line), size):
        await client.write_gatt_char(
            characteristic,
            line[offset : offset + size],
            response=response,
        )
        chunks += 1
    return chunks


async def _find_device(name_prefix: str, timeout: float) -> Any:
    from bleak import BleakScanner

    service = NUS_SERVICE_UUID.lower()

    def matches(device: Any, advertisement: Any) -> bool:
        name = getattr(advertisement, "local_name", None) or getattr(
            device, "name", None
        )
        services = {
            item.lower()
            for item in (getattr(advertisement, "service_uuids", None) or [])
        }
        return isinstance(name, str) and name.startswith(name_prefix) and service in services

    device = await BleakScanner.find_device_by_filter(matches, timeout=timeout)
    if device is None:
        raise RuntimeError(f"no advertising panel matched {name_prefix}")
    return device


async def measure(args: argparse.Namespace) -> int:
    from bleak import BleakClient
    from importlib.metadata import version

    # Exclusive creation prevents a repeat run from replacing previous evidence.
    output = args.output.open("x", encoding="utf-8") if args.output else nullcontext(None)
    with output as stream:
        def emit(record: dict[str, Any]) -> None:
            line = json.dumps(record, ensure_ascii=True, allow_nan=False)
            print(line, flush=True)
            if stream is not None:
                stream.write(line + "\n")
                stream.flush()

        emit({"type": "run", "started_utc": datetime.now(timezone.utc).isoformat(),
              "target_expected": args.target, "name_prefix": args.name_prefix,
              "response": args.response, "chunk_cap": args.chunk_cap,
              "payload_bytes": args.payload_bytes, "iterations": args.iterations,
              "warmup": args.warmup, "bleak_version": version("bleak")})
        samples: list[dict[str, Any]] = []
        completed = False
        measured_start = None
        measured_end = None
        try:
            device = await _find_device(args.name_prefix, args.timeout)
            inbox = LineInbox()

            def notification(_sender: Any, data: bytearray | bytes) -> None:
                inbox.feed(data)

            async with BleakClient(device, pair=False, timeout=args.timeout,
                                   winrt={"use_cached_services": False}) as client:
                rx = client.services.get_characteristic(NUS_RX_UUID)
                tx = client.services.get_characteristic(NUS_TX_UUID)
                if rx is None or tx is None:
                    raise RuntimeError("panel does not expose Nordic UART Service")
                await client.start_notify(tx, notification)
                async with asyncio.timeout(args.timeout):
                    await _write_line(client, rx, b'{"cmd":"status"}\n', cap=180, response=True)
                    status = await inbox.receive_reply("status", args.timeout)
                data = status.get("data")
                if status.get("ok") is not True or not isinstance(data, dict):
                    raise RuntimeError("panel did not return a valid status response")
                if data.get("target") != args.target or data.get("sec") is not True:
                    raise RuntimeError("target identity or encrypted-link check failed")
                emit({"type": "link", "target": data["target"],
                      "firmware": data.get("fw"), "encrypted": True,
                      "mtu": client.mtu_size,
                      "write_size": _write_size(client, rx, args.chunk_cap, args.response)})
                for iteration in range(args.warmup + args.iterations):
                    if iteration == args.warmup:
                        measured_start = time.perf_counter()
                    line = build_probe_line(iteration, args.payload_bytes)
                    command = json.loads(line)["cmd"]
                    sample = {"type": "sample", "index": iteration,
                              "warmup": iteration < args.warmup,
                              "payload_bytes": args.payload_bytes, "line_bytes": len(line),
                              "success": False}
                    started = time.perf_counter()
                    try:
                        async with asyncio.timeout(args.timeout):
                            chunks = await _write_line(client, rx, line, cap=args.chunk_cap,
                                                       response=args.response)
                            written = time.perf_counter()
                            reply = await inbox.receive_reply(command, args.timeout)
                            validate_probe_reply(reply, command)
                        sample.update(success=True, chunks=chunks,
                                      write_s=written - started,
                                      ack_wait_s=time.perf_counter() - written)
                    except Exception as exc:
                        sample["error"] = type(exc).__name__
                        raise
                    finally:
                        measured_end = time.perf_counter()
                        sample["elapsed_s"] = measured_end - started
                        samples.append(sample)
                        emit(sample)
                completed = True
        except Exception as exc:
            # Partial lines after a failed write require a fresh connection.
            emit({"type": "run_error", "error": type(exc).__name__})
        finally:
            duration = (measured_end - measured_start
                        if measured_start is not None and measured_end is not None else None)
            summary = summarize_samples(samples, duration)
            summary["completed"] = completed
            summary["requested"] = args.iterations
            emit(summary)
        return 0 if completed else 1


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--name-prefix", required=True)
    parser.add_argument("--chunk-cap", type=int, choices=(180, 512), required=True)
    parser.add_argument("--response", action="store_true")
    parser.add_argument("--target", default="waveshare_amoled_216")
    parser.add_argument("--payload-bytes", type=int, default=PAYLOAD_BYTES)
    parser.add_argument("--warmup", type=int, default=5)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--iterations", type=int, default=5)
    parser.add_argument("--timeout", type=float, default=DEFAULT_TIMEOUT_SECONDS)
    args = parser.parse_args(argv)
    if args.warmup < 0 or not 1 <= args.payload_bytes <= PAYLOAD_BYTES:
        parser.error("warmup must be non-negative and payload-bytes must be 1..2880")
    if args.iterations < 1:
        parser.error("--iterations must be positive")
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    return args


def main(argv: list[str] | None = None) -> int:
    try:
        return asyncio.run(measure(parse_args(argv)))
    except (TimeoutError, RuntimeError, OSError) as exc:
        print(f"probe failed: {exc}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
