"""Command-line entry point for the standalone Bridge.

Windows and macOS differ in three places only: which CodexBar CLI dialect the
usage source speaks, who runs the pairing ceremony, and how the six-digit code
reaches that ceremony. Everything below the transport is shared.
"""

from __future__ import annotations

import argparse
import asyncio
import logging
import sys
from pathlib import Path
from typing import Sequence

from quotaframe_bridge import __version__
from quotaframe_bridge.cli.instance_lock import (
    BridgeAlreadyRunningError,
    BridgeInstanceLock,
)
from quotaframe_bridge.pairing.base import DevicePairer
from quotaframe_bridge.service.devices import TrayServiceGraph
from quotaframe_bridge.service.device_manager import DeviceManager
from quotaframe_bridge.service.multi_device import (
    DeviceSession,
    MultiDeviceBridgeService,
)
from quotaframe_bridge.sources.mock import MockUsageSource
from quotaframe_bridge.sources.selection import (
    UnsupportedPlatformError,
    create_usage_source,
)
from quotaframe_bridge.service.adoption import discover_device
from quotaframe_bridge.service.ownership import OwnershipStore, display_name
from quotaframe_bridge.transports.console import ConsoleTransport


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="quotaframe-bridge",
        description="Publish Codex and Claude usage to a supported panel over BLE.",
    )
    parser.add_argument(
        "--version",
        action="version",
        version=f"QuotaFrame Bridge {__version__}",
    )
    parser.add_argument("--mock", action="store_true", help="use deterministic mock usage")
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="print sanitized mock protocol lines instead of using BLE",
    )
    parser.add_argument("--cycles", type=int, help="stop after this many collections")
    parser.add_argument("--interval", type=float, default=60.0)
    parser.add_argument("--publish-interval", type=float, default=30.0)
    parser.add_argument("--codexbar-cli", type=Path)
    parser.add_argument("--device-name")
    parser.add_argument(
        "--name-prefix",
        help=(
            "use single-device mode for one advertising-name prefix; "
            "by default one session is maintained for each adopted device"
        ),
    )
    ownership_actions = parser.add_mutually_exclusive_group()
    ownership_actions.add_argument(
        "--add-device",
        action="store_true",
        help=(
            "scan for a panel that is not adopted yet, pair with it, record "
            "it, and exit; adding a device is always an explicit action"
        ),
    )
    ownership_actions.add_argument(
        "--list-devices",
        action="store_true",
        help="print the adopted panels and exit",
    )
    ownership_actions.add_argument(
        "--forget-device",
        metavar="ADDRESS",
        help="remove one adopted panel by address and exit",
    )
    parser.add_argument("--scan-timeout", type=float, default=10.0)
    parser.add_argument("--connect-timeout", type=float, default=60.0)
    parser.add_argument(
        "--repair-pairing",
        action="store_true",
        help=(
            "Windows only: remove the bond once, then perform secure PIN "
            "pairing; on macOS the bond can only be removed from Bluetooth "
            "settings"
        ),
    )
    parser.add_argument(
        "--source-timeout",
        type=float,
        help=(
            "seconds one CodexBar collection may take; the default depends on "
            "the platform's CLI (Windows 15, macOS 90)"
        ),
    )
    parser.add_argument("--ack-timeout", type=float, default=5.0)
    parser.add_argument(
        "--log-level",
        choices=("DEBUG", "INFO", "WARNING", "ERROR"),
        default="INFO",
    )
    return parser


def create_pairer(
    *,
    repair: bool,
    timeout: float,
    ceremony_lock: asyncio.Lock | None = None,
    platform: str | None = None,
) -> DevicePairer:
    """Return the pairer that owns the passkey ceremony on this platform."""

    platform = sys.platform if platform is None else platform
    if platform == "win32":
        from quotaframe_bridge.pairing.windows import WindowsPairer

        return WindowsPairer(
            prompt_for_pin,
            repair=repair,
            timeout=timeout,
            ceremony_lock=ceremony_lock,
        )
    if platform == "darwin":
        from quotaframe_bridge.pairing.darwin import DarwinPairer

        # No ceremony lock: macOS serializes its own pairing dialogs, and this
        # pairer never runs one.
        return DarwinPairer(repair=repair)
    raise UnsupportedPlatformError(platform)


async def prompt_for_pin(device_name: str) -> str:
    """Read the displayed panel passkey without logging or retaining it."""

    try:
        import msvcrt
    except ImportError:
        raise RuntimeError("interactive pairing requires a Windows console") from None

    prompt = f"Enter the six-digit code shown on {device_name}: "
    sys.stdout.write(prompt)
    sys.stdout.flush()
    characters: list[str] = []
    try:
        while True:
            if not msvcrt.kbhit():
                await asyncio.sleep(0.05)
                continue
            character = msvcrt.getwch()
            if character in ("\x00", "\xe0"):
                if msvcrt.kbhit():
                    msvcrt.getwch()
                continue
            if character in ("\r", "\n"):
                sys.stdout.write("\n")
                sys.stdout.flush()
                return "".join(characters)
            if character == "\b":
                if characters:
                    characters.pop()
                    sys.stdout.write("\b \b")
                    sys.stdout.flush()
                continue
            if character == "\x03":
                raise KeyboardInterrupt
            if character.isprintable():
                characters.append(character)
                sys.stdout.write(character)
                sys.stdout.flush()
    except asyncio.CancelledError:
        sys.stdout.write("\n")
        sys.stdout.flush()
        raise


def _print_devices(store: OwnershipStore) -> int:
    owned = store.devices
    if store.read_error:
        print("device records unreadable; see log", file=sys.stderr)
        return 1
    if not owned:
        print("no panel has been adopted yet; run with --add-device")
        return 0
    for device in owned:
        print(f"{display_name(device, owned)}\t{device.address}\t{device.firmware_project or 'undeclared'}\t{device.target or 'undeclared'}")
    return 0


def _forget_device(store: OwnershipStore, address: str) -> int:
    """Drop one panel from the Bridge's own list.

    The platform bond is left alone: Windows and macOS both own that, and
    removing it is what --repair-pairing and the macOS guidance are for.
    """

    if store.remove(address):
        print(f"removed {address}")
        return 0
    print(f"no adopted panel has address {address}")
    return 1


async def _add_device(args: argparse.Namespace, store: OwnershipStore) -> int:
    """Explicitly adopt one securely negotiated panel, then exit."""

    excluded = store.addresses()
    if store.read_error:
        raise RuntimeError("device records unreadable; see log")
    if sys.platform == "darwin":
        from quotaframe_bridge.macos_bluetooth import preflight

        preflight()
    print("looking for a panel; confirm the six-digit code shown on its screen…")
    discovery = await discover_device(
        pairer=create_pairer(
            repair=args.repair_pairing,
            timeout=args.connect_timeout,
        ),
        exclude_addresses=excluded,
        scan_timeout=args.scan_timeout,
        device_name=args.device_name,
        name_prefix=args.name_prefix,
    )
    if not discovery.found:
        print("no new panel answered; check that it is powered and in range")
        return 1
    device = store.add(discovery.target, discovery.address, name=discovery.name,
                       firmware_project=discovery.firmware_project)
    if device is None:
        print(f"panel {discovery.address} could not be adopted")
        return 1
    print(f"adopted {device.name} ({device.target}) at {device.address}")
    return 0


async def _run(args: argparse.Namespace) -> None:
    source = (
        MockUsageSource()
        if args.mock
        else create_usage_source(
            args.codexbar_cli,
            timeout=args.source_timeout,
        )
    )
    if args.dry_run:
        transport = ConsoleTransport()
        service = MultiDeviceBridgeService(
            source,
            (DeviceSession("Console", transport),),
            interval=args.interval,
            publish_interval=args.publish_interval,
        )
    else:
        from quotaframe_bridge.transports.bleak_nus import BleakNusTransport

        if sys.platform == "darwin":
            from quotaframe_bridge.macos_bluetooth import preflight

            preflight()

        if args.device_name is None and args.name_prefix is None:
            store = OwnershipStore()
            owned = store.devices
            if store.read_error:
                raise RuntimeError("device records unreadable; see log")
            if not owned:
                raise RuntimeError("no panel has been adopted yet; run with --add-device")
            ceremony_lock = asyncio.Lock()
            service = MultiDeviceBridgeService(
                source, (), interval=args.interval,
                publish_interval=args.publish_interval,
            )
            graph = TrayServiceGraph(
                service=service, store=store,
                pairer_factory=lambda: create_pairer(
                    repair=False, timeout=args.connect_timeout,
                    ceremony_lock=ceremony_lock,
                ),
                scan_timeout=args.scan_timeout,
                connect_timeout=args.connect_timeout,
                ack_timeout=args.ack_timeout,
            )
            graph.build()
        else:
            pairer = create_pairer(
                repair=args.repair_pairing,
                timeout=args.connect_timeout,
            )
            transport = DeviceManager(
                lambda: BleakNusTransport(
                    pairer=pairer,
                    device_name=args.device_name,
                    name_prefix=args.name_prefix,
                    scan_timeout=args.scan_timeout,
                    connect_timeout=args.connect_timeout,
                    ack_timeout=args.ack_timeout,
                )
            )
            service = MultiDeviceBridgeService(
                source,
                (
                    DeviceSession(
                        args.device_name or args.name_prefix or "Panel",
                        transport,
                    ),
                ),
                interval=args.interval,
                publish_interval=args.publish_interval,
            )
    cycles = args.cycles
    if args.dry_run and cycles is None:
        cycles = 1
    await service.run(cycles=cycles)


def main(argv: Sequence[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    if args.dry_run and not args.mock:
        parser.error("--dry-run requires --mock")
    if args.dry_run and args.add_device:
        parser.error("--add-device cannot be used with --dry-run")
    if args.cycles is not None and args.cycles < 1:
        parser.error("--cycles must be at least one")
    if args.interval < 0:
        parser.error("--interval must be non-negative")
    if (
        args.repair_pairing
        and not args.dry_run
        and args.device_name is None
        and args.name_prefix is None
    ):
        parser.error(
            "--repair-pairing requires --device-name or --name-prefix"
        )
    logging.basicConfig(
        level=getattr(logging, args.log_level),
        format="%(asctime)s %(levelname)s %(name)s: %(message)s",
    )
    # The ownership commands are complete actions, not modifiers: each one
    # reports and exits rather than falling through into a publish loop.
    store = OwnershipStore()
    if args.list_devices:
        return _print_devices(store)
    if args.forget_device:
        try:
            with BridgeInstanceLock.acquire():
                return _forget_device(store, args.forget_device)
        except BridgeAlreadyRunningError as exc:
            logging.getLogger(__name__).error("%s", exc)
            return 1
        except Exception as exc:
            logging.getLogger(__name__).error("%s", exc)
            return 1
    if args.add_device:
        try:
            with BridgeInstanceLock.acquire():
                return asyncio.run(_add_device(args, store))
        except BridgeAlreadyRunningError as exc:
            logging.getLogger(__name__).error("%s", exc)
            return 1
        except KeyboardInterrupt:
            return 130
        except Exception as exc:
            logging.getLogger(__name__).error("%s", exc)
            return 1
    try:
        with BridgeInstanceLock.acquire():
            asyncio.run(_run(args))
    except BridgeAlreadyRunningError as exc:
        logging.getLogger(__name__).error("%s", exc)
        return 1
    except KeyboardInterrupt:
        return 130
    except Exception as exc:
        logging.getLogger(__name__).error("%s", exc)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
