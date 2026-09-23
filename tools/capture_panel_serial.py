"""Capture panel serial output for a fixed interval without sending reset or data."""

from __future__ import annotations

import argparse
import json
import time
from datetime import datetime, timezone
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--seconds", type=float, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.seconds <= 0:
        parser.error("seconds must be positive")
    import serial

    started = datetime.now(timezone.utc).isoformat()
    count = 0
    # Control lines are configured before opening; no reset pulse is requested.
    port = serial.Serial(port=None, baudrate=115200, timeout=.2)
    port.dtr = False
    port.rts = False
    port.port = args.port
    with args.output.open("xb") as output:
        try:
            port.open()
            begin = time.monotonic()
            while time.monotonic() - begin < args.seconds:
                data = port.read(max(1, port.in_waiting))
                if data:
                    output.write(data)
                    output.flush()
                    count += len(data)
        finally:
            port.close()
    print(json.dumps({"port": args.port, "started_utc": started,
                      "ended_utc": datetime.now(timezone.utc).isoformat(),
                      "requested_seconds": args.seconds, "bytes": count,
                      "output": str(args.output), "reset_requested": False}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
