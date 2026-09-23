from __future__ import annotations

import asyncio
import io
import sys
import tempfile
from contextlib import nullcontext
from pathlib import Path
import unittest
from unittest.mock import AsyncMock, patch

from quotaframe_bridge.cli.main import (
    _add_device,
    _run,
    build_parser,
    create_pairer,
    main,
    prompt_for_pin,
)
from quotaframe_bridge.service.adoption import Discovery
from quotaframe_bridge.pairing.base import PairingGuidanceError
from quotaframe_bridge.sources.selection import UnsupportedPlatformError
from quotaframe_bridge.service.ownership import OwnedDevice, OwnershipStore


class _FakeStore:
    """An ownership store with a fixed device list."""

    def __init__(self, devices: tuple[OwnedDevice, ...]) -> None:
        self.devices = devices
        self.read_error = False

windows_console_only = unittest.skipUnless(
    sys.platform == "win32",
    "the console PIN prompt reads Windows console keystrokes",
)


def on_platform(name: str):
    """Run the pairer dispatch as if the Bridge were hosted on `name`."""

    return patch("quotaframe_bridge.cli.main.sys.platform", name)


class CliPairingParserTests(unittest.TestCase):
    def test_parser_accepts_explicit_repair_flag(self) -> None:
        args = build_parser().parse_args(["--mock", "--repair-pairing"])

        self.assertTrue(args.repair_pairing)

    def test_parser_uses_dual_target_discovery_by_default(self) -> None:
        args = build_parser().parse_args(["--mock"])

        self.assertIsNone(args.name_prefix)

    def test_parser_keeps_explicit_prefix_override(self) -> None:
        args = build_parser().parse_args(
            ["--mock", "--name-prefix", "Lab-Panel-"]
        )

        self.assertEqual(args.name_prefix, "Lab-Panel-")

    def test_parser_rejects_conflicting_ownership_actions(self) -> None:
        for arguments in (
            ["--list-devices", "--add-device"],
            ["--list-devices", "--forget-device", "AA:BB:CC:DD:EE:01"],
            ["--add-device", "--forget-device", "AA:BB:CC:DD:EE:01"],
        ):
            with self.subTest(arguments=arguments), self.assertRaises(SystemExit):
                build_parser().parse_args(arguments)


class CliPairingRuntimeTests(unittest.IsolatedAsyncioTestCase):
    def test_default_multi_device_mode_rejects_unscoped_repair(self) -> None:
        error = io.StringIO()

        class RejectLock:
            @classmethod
            def acquire(cls):
                raise AssertionError("invalid repair acquired instance lock")

        def reject_runtime(coroutine) -> None:
            coroutine.close()
            raise AssertionError("invalid repair reached runtime")

        with (
            patch("sys.stderr", error),
            patch(
                "quotaframe_bridge.cli.main.BridgeInstanceLock",
                RejectLock,
                create=True,
            ),
            patch(
                "quotaframe_bridge.cli.main.asyncio.run",
                side_effect=reject_runtime,
            ),
            self.assertRaises(SystemExit) as caught,
        ):
            main(["--mock", "--repair-pairing"])

        self.assertEqual(caught.exception.code, 2)
        self.assertIn("--device-name", error.getvalue())
        self.assertIn("--name-prefix", error.getvalue())

    def test_valid_command_holds_instance_lock_around_runtime(self) -> None:
        events: list[str] = []

        class FakeLock:
            def __enter__(self):
                events.append("entered")
                return self

            def __exit__(self, exc_type, exc, traceback) -> None:
                events.append("exited")

        class FakeLockType:
            @classmethod
            def acquire(cls):
                events.append("acquired")
                return FakeLock()

        def run_runtime(coroutine) -> None:
            events.append("runtime")
            coroutine.close()

        with (
            patch(
                "quotaframe_bridge.cli.main.BridgeInstanceLock",
                FakeLockType,
                create=True,
            ),
            patch(
                "quotaframe_bridge.cli.main.asyncio.run",
                side_effect=run_runtime,
            ),
        ):
            result = main(["--mock", "--dry-run"])

        self.assertEqual(result, 0)
        self.assertEqual(
            events,
            ["acquired", "entered", "runtime", "exited"],
        )

    @windows_console_only
    async def test_terminal_provider_uses_visible_console_input(self) -> None:
        output = io.StringIO()
        keys = iter(("0", "4", "2", "7", "3", "1", "\r"))

        with (
            patch("builtins.input", side_effect=AssertionError("blocking input used")),
            patch("msvcrt.kbhit", return_value=True),
            patch("msvcrt.getwch", side_effect=lambda: next(keys)),
            patch("sys.stdout", output),
        ):
            result = await prompt_for_pin("M5-Usage-D86A")

        self.assertEqual(result, "042731")
        self.assertIn("M5-Usage-D86A", output.getvalue())
        self.assertIn("042731", output.getvalue())

    @windows_console_only
    async def test_terminal_provider_is_cancellable_without_an_input_thread(
        self,
    ) -> None:
        output = io.StringIO()
        with (
            patch("builtins.input", side_effect=AssertionError("blocking input used")),
            patch("msvcrt.kbhit", return_value=False),
            patch("sys.stdout", output),
        ):
            task = asyncio.create_task(prompt_for_pin("M5-Usage-D86A"))
            await asyncio.sleep(0)
            task.cancel()
            with self.assertRaises(asyncio.CancelledError):
                await task

    async def test_dry_run_never_constructs_windows_pairer(self) -> None:
        args = build_parser().parse_args(["--mock", "--dry-run"])

        with (
            on_platform("win32"),
            patch(
                "quotaframe_bridge.pairing.windows.WindowsPairer",
                side_effect=AssertionError("dry-run constructed WindowsPairer"),
            ),
        ):
            await _run(args)

    async def test_explicit_adoption_honors_repair_and_device_filter(self) -> None:
        args = build_parser().parse_args(
            [
                "--add-device",
                "--device-name",
                "M5-Usage-D86A",
                "--repair-pairing",
                "--connect-timeout",
                "12",
                "--scan-timeout",
                "3",
            ]
        )
        pairer = object()
        with tempfile.TemporaryDirectory() as directory:
            store = OwnershipStore(Path(directory) / "devices.json")
            with (
                patch("quotaframe_bridge.macos_bluetooth.preflight"),
                patch(
                    "quotaframe_bridge.cli.main.create_pairer",
                    return_value=pairer,
                ) as create,
                patch(
                    "quotaframe_bridge.cli.main.discover_device",
                    new=AsyncMock(
                        return_value=Discovery(
                            target="m5sticks3", name="M5StickS3",
                            address="AA:BB:CC:DD:EE:01",
                        )
                    ),
                ) as discover,
                patch("sys.stdout", io.StringIO()),
            ):
                result = await _add_device(args, store)

        self.assertEqual(result, 0)
        create.assert_called_once_with(repair=True, timeout=12.0)
        discover.assert_awaited_once_with(
            pairer=pairer,
            exclude_addresses=frozenset(),
            scan_timeout=3.0,
            device_name="M5-Usage-D86A",
            name_prefix=None,
        )

    async def test_reconnect_transports_share_one_repair_pairer(self) -> None:
        captured_pairer_arguments: list[tuple[object, bool, float]] = []
        transport_pairers: list[object] = []
        pairer = object()

        class FakePairer:
            def __new__(
                cls,
                provider,
                *,
                repair: bool,
                timeout: float,
                ceremony_lock=None,
            ):
                captured_pairer_arguments.append((provider, repair, timeout))
                return pairer

        class FakeBleakTransport:
            def __init__(self, **kwargs) -> None:
                transport_pairers.append(kwargs["pairer"])

        class FakeManager:
            def __init__(self, factory) -> None:
                self.factory = factory

        class FakeSession:
            def __init__(self, label, transport) -> None:
                self.transport = transport

        class FakeMultiService:
            def __init__(self, source, sessions, **kwargs) -> None:
                self.sessions = sessions

            async def run(self, *, cycles) -> None:
                session = self.sessions[0]
                session.transport.factory()
                session.transport.factory()

        args = build_parser().parse_args(
            [
                "--mock",
                "--repair-pairing",
                "--device-name",
                "M5-Usage-D86A",
                "--connect-timeout",
                "37",
                "--cycles",
                "1",
            ]
        )

        with (
            on_platform("win32"),
            patch(
                "quotaframe_bridge.pairing.windows.WindowsPairer",
                FakePairer,
            ),
            patch(
                "quotaframe_bridge.transports.bleak_nus.BleakNusTransport",
                FakeBleakTransport,
            ),
            patch("quotaframe_bridge.cli.main.DeviceManager", FakeManager),
            patch("quotaframe_bridge.cli.main.DeviceSession", FakeSession),
            patch(
                "quotaframe_bridge.cli.main.MultiDeviceBridgeService",
                FakeMultiService,
            ),
        ):
            await _run(args)

        self.assertEqual(
            captured_pairer_arguments,
            [(prompt_for_pin, True, 37.0)],
        )
        self.assertEqual(transport_pairers, [pairer, pairer])

    async def test_default_runtime_refuses_to_run_with_nothing_adopted(self) -> None:
        """The CLI never adopts as a side effect; it says so instead."""

        args = build_parser().parse_args(["--mock", "--cycles", "1"])

        with (
            on_platform("win32"),
            patch(
                "quotaframe_bridge.cli.main.OwnershipStore",
                lambda *a, **k: _FakeStore(()),
            ),
            self.assertRaises(RuntimeError) as raised,
        ):
            await _run(args)

        self.assertIn("--add-device", str(raised.exception))

    async def test_default_runtime_builds_one_session_per_adopted_device(self) -> None:
        pairers: list[object] = []
        pairer_arguments: list[dict[str, object]] = []
        transports: list[dict[str, object]] = []
        sessions: list[tuple[str, object]] = []

        class FakePairer:
            def __init__(self, provider, **kwargs) -> None:
                pairers.append(self)
                pairer_arguments.append({"provider": provider, **kwargs})

        class FakeBleakTransport:
            def __init__(self, **kwargs) -> None:
                transports.append(kwargs)

        class FakeManager:
            def __init__(self, factory, **kwargs) -> None:
                self.factory = factory

        class FakeSession:
            def __init__(self, label, transport) -> None:
                sessions.append((label, transport))
                self.label = label
                self.transport = transport

        class FakeMultiService:
            def __init__(self, source, actual_sessions, **kwargs) -> None:
                self.sessions = list(actual_sessions)

            def add_session(self, session):
                self.sessions.append(session)

            async def run(self, *, cycles) -> None:
                for session in self.sessions:
                    session.transport.factory()

        owned = (
            OwnedDevice("m5sticks3", "AA:BB:CC:DD:EE:01", name="M5StickS3"),
            OwnedDevice("waveshare_amoled_216", "AA:BB:CC:DD:EE:02", name="Waveshare AMOLED 2.16"),
        )
        args = build_parser().parse_args(["--mock", "--cycles", "1"])

        with (
            on_platform("win32"),
            patch(
                "quotaframe_bridge.cli.main.OwnershipStore",
                lambda *a, **k: _FakeStore(owned),
            ),
            patch(
                "quotaframe_bridge.pairing.windows.WindowsPairer",
                FakePairer,
            ),
            patch(
                "quotaframe_bridge.service.devices.BleakNusTransport",
                FakeBleakTransport,
            ),
            patch("quotaframe_bridge.service.devices.DeviceManager", FakeManager),
            patch(
                "quotaframe_bridge.service.devices.DeviceSession",
                FakeSession,
                create=True,
            ),
            patch(
                "quotaframe_bridge.cli.main.MultiDeviceBridgeService",
                FakeMultiService,
                create=True,
            ),
        ):
            await _run(args)

        self.assertEqual(len(pairers), len(owned))
        self.assertEqual(len({id(pairer) for pairer in pairers}), len(owned))
        self.assertEqual(
            len({id(arguments["ceremony_lock"]) for arguments in pairer_arguments}),
            1,
        )
        self.assertTrue(all(not arguments["repair"] for arguments in pairer_arguments))
        self.assertEqual(len(sessions), len(owned))
        # Sessions are routed by address and verified by target, not by the
        # advertising name prefix.
        self.assertEqual(
            {item["address"] for item in transports},
            {device.address for device in owned},
        )
        self.assertEqual(
            {item.get("expected_target") for item in transports},
            {None},
        )
        self.assertEqual({item["device_name"] for item in transports}, {None})
        self.assertNotIn("name_prefix", transports[0])

    async def test_explicit_prefix_keeps_single_device_runtime(self) -> None:
        captured_prefixes: list[str | None] = []
        sessions: list[tuple[str, object]] = []

        class FakePairer:
            def __init__(self, provider, **kwargs) -> None:
                pass

        class FakeBleakTransport:
            def __init__(self, **kwargs) -> None:
                captured_prefixes.append(kwargs["name_prefix"])

        class FakeManager:
            def __init__(self, factory) -> None:
                self.factory = factory

        class FakeSession:
            def __init__(self, label, transport) -> None:
                sessions.append((label, transport))
                self.label = label
                self.transport = transport

        class FakeMultiService:
            def __init__(self, source, actual_sessions, **kwargs) -> None:
                self.sessions = actual_sessions

            async def run(self, *, cycles) -> None:
                for session in self.sessions:
                    session.transport.factory()

        args = build_parser().parse_args(
            [
                "--mock",
                "--name-prefix",
                "WS-Usage-",
                "--cycles",
                "1",
            ]
        )

        with (
            on_platform("win32"),
            patch(
                "quotaframe_bridge.pairing.windows.WindowsPairer",
                FakePairer,
            ),
            patch(
                "quotaframe_bridge.transports.bleak_nus.BleakNusTransport",
                FakeBleakTransport,
            ),
            patch("quotaframe_bridge.cli.main.DeviceManager", FakeManager),
            patch(
                "quotaframe_bridge.cli.main.DeviceSession",
                FakeSession,
            ),
            patch(
                "quotaframe_bridge.cli.main.MultiDeviceBridgeService",
                FakeMultiService,
            ),
        ):
            await _run(args)

        self.assertEqual(captured_prefixes, ["WS-Usage-"])
        self.assertEqual(len(sessions), 1)


class PairerDispatchTests(unittest.IsolatedAsyncioTestCase):
    def test_windows_host_selects_the_winrt_ceremony(self) -> None:
        from quotaframe_bridge.pairing.windows import WindowsPairer

        pairer = create_pairer(repair=False, timeout=30.0, platform="win32")

        self.assertIsInstance(pairer, WindowsPairer)

    def test_macos_host_selects_the_system_driven_pairer(self) -> None:
        from quotaframe_bridge.pairing.darwin import DarwinPairer

        pairer = create_pairer(repair=False, timeout=30.0, platform="darwin")

        self.assertIsInstance(pairer, DarwinPairer)

    def test_unsupported_host_is_rejected_before_any_scan(self) -> None:
        with self.assertRaises(UnsupportedPlatformError):
            create_pairer(repair=False, timeout=30.0, platform="linux")

    async def test_macos_pairing_defers_to_the_system_dialog(self) -> None:
        pairer = create_pairer(repair=False, timeout=30.0, platform="darwin")

        # No ceremony at all: the encrypted GATT read triggers macOS's own
        # passkey dialog, so a successful "pairing" is a no-op here.
        await pairer.ensure_paired(object())

    async def test_macos_repair_explains_what_the_user_must_do(self) -> None:
        pairer = create_pairer(repair=True, timeout=30.0, platform="darwin")

        with self.assertRaises(PairingGuidanceError) as raised:
            await pairer.ensure_paired(object())

        self.assertIn("Forget This Device", str(raised.exception))


class OwnershipCommandTests(unittest.TestCase):
    """--list-devices / --forget-device / --add-device act and exit."""

    def setUp(self) -> None:
        self._temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self._temporary.cleanup)
        self.path = Path(self._temporary.name) / "devices.json"

    def run_main(self, argv: list[str]) -> tuple[int, str]:
        buffer = io.StringIO()
        store = OwnershipStore(self.path)
        with (
            patch(
                "quotaframe_bridge.cli.main.OwnershipStore",
                lambda *a, **k: store,
            ),
            patch(
                "quotaframe_bridge.cli.main.BridgeInstanceLock.acquire",
                return_value=nullcontext(),
            ),
            patch("sys.stdout", buffer),
        ):
            code = main(argv)
        return code, buffer.getvalue()

    def test_listing_nothing_explains_how_to_adopt(self) -> None:
        code, output = self.run_main(["--list-devices"])

        self.assertEqual(code, 0)
        self.assertIn("--add-device", output)

    def test_dry_run_rejects_add_device_before_bluetooth_work(self) -> None:
        error = io.StringIO()
        with (
            patch("sys.stderr", error),
            patch(
                "quotaframe_bridge.cli.main._add_device",
                side_effect=AssertionError("dry-run started Bluetooth adoption"),
            ),
            self.assertRaises(SystemExit) as raised,
        ):
            main(["--mock", "--dry-run", "--add-device"])

        self.assertEqual(raised.exception.code, 2)
        self.assertIn("--add-device", error.getvalue())
        self.assertIn("--dry-run", error.getvalue())

    def test_listing_shows_target_and_address_for_each_panel(self) -> None:
        OwnershipStore(self.path).add("m5sticks3", "AA:BB:CC:DD:EE:01", name="M5StickS3")

        code, output = self.run_main(["--list-devices"])

        self.assertEqual(code, 0)
        self.assertIn("m5sticks3", output)
        self.assertIn("AA:BB:CC:DD:EE:01", output)

    def test_forgetting_a_known_panel_removes_it(self) -> None:
        OwnershipStore(self.path).add("m5sticks3", "AA:BB:CC:DD:EE:01", name="M5StickS3")

        code, output = self.run_main(["--forget-device", "aa:bb:cc:dd:ee:01"])

        self.assertEqual(code, 0)
        self.assertIn("removed", output)
        self.assertEqual(OwnershipStore(self.path).devices, ())

    def test_forgetting_holds_instance_lock_around_the_file_mutation(self) -> None:
        store = OwnershipStore(self.path)
        store.add("m5sticks3", "AA:BB:CC:DD:EE:01", name="M5StickS3")
        events: list[str] = []
        original_remove = store.remove

        def remove(address: str) -> bool:
            events.append("remove")
            return original_remove(address)

        store.remove = remove  # type: ignore[method-assign]

        class Lock:
            def __enter__(self):
                events.append("enter")
                return self

            def __exit__(self, exc_type, exc, traceback) -> None:
                events.append("exit")

        class LockType:
            @classmethod
            def acquire(cls):
                events.append("acquire")
                return Lock()

        with (
            patch(
                "quotaframe_bridge.cli.main.OwnershipStore",
                return_value=store,
            ),
            patch(
                "quotaframe_bridge.cli.main.BridgeInstanceLock",
                LockType,
            ),
            patch("sys.stdout", io.StringIO()),
        ):
            code = main(["--forget-device", "AA:BB:CC:DD:EE:01"])

        self.assertEqual(code, 0)
        self.assertEqual(events, ["acquire", "enter", "remove", "exit"])

    def test_forgetting_an_unknown_panel_fails_loudly(self) -> None:
        code, output = self.run_main(["--forget-device", "AA:BB:CC:DD:EE:99"])

        self.assertEqual(code, 1)
        self.assertIn("no adopted panel", output)


if __name__ == "__main__":
    unittest.main()
