from __future__ import annotations

import asyncio
import threading
import unittest
from types import SimpleNamespace
from unittest.mock import patch

from quotaframe_bridge.pairing.windows import (
    PairingBusyError,
    PairingError,
    WindowsPairer,
    validate_pin,
)


async def async_value(value):
    return value


class FakeBleDevice:
    def __init__(self, address: str) -> None:
        self.address = address


class FakeDeferral:
    def __init__(self, projection: "FakeProjection") -> None:
        self._projection = projection

    def complete(self) -> None:
        self._projection.completed_deferrals += 1
        self._projection.deferral_completed.set()


class FakePairingArgs:
    def __init__(self, projection: "FakeProjection", pairing_kind: object) -> None:
        self._projection = projection
        self.pairing_kind = pairing_kind

    def get_deferral(self) -> FakeDeferral:
        return FakeDeferral(self._projection)

    def accept_with_pin(self, pin: str) -> None:
        self._projection.accepted_pins.append(pin)


class FakeWinRtOperation:
    def __init__(self, coroutine) -> None:
        self._coroutine = coroutine
        self.cancel_calls = 0

    def __await__(self):
        return self._coroutine.__await__()

    def cancel(self) -> None:
        self.cancel_calls += 1


class FakeCustomPairing:
    def __init__(self, projection: "FakeProjection") -> None:
        self._projection = projection
        self._handler = None

    def add_pairing_requested(self, handler):
        self._handler = handler
        self._projection.handler_adds += 1
        return 27

    def remove_pairing_requested(self, token) -> None:
        if token != 27:
            raise AssertionError("unexpected event token")
        self._projection.handler_removes += 1
        self._handler = None

    def pair_with_protection_level_async(
        self,
        pairing_kind: object,
        protection_level: object,
    ):
        operation = FakeWinRtOperation(
            self._run_pairing(pairing_kind, protection_level)
        )
        self._projection.last_operation = operation
        return operation

    async def _run_pairing(
        self,
        pairing_kind: object,
        protection_level: object,
    ):
        self._projection.pair_calls.append((pairing_kind, protection_level))
        self._projection.deferral_completed.clear()
        if self._handler is None:
            raise AssertionError("pairing handler was not registered")
        if (
            self._projection.pair_status
            == self._projection.OPERATION_ALREADY_IN_PROGRESS
        ):
            return SimpleNamespace(status=self._projection.pair_status)
        requested_kind = (
            self._projection.requested_kind
            if self._projection.requested_kind is not None
            else pairing_kind
        )
        args = FakePairingArgs(self._projection, requested_kind)
        if self._projection.threaded_handler:
            thread = threading.Thread(
                target=self._handler,
                args=(self, args),
                daemon=True,
            )
            self._projection.handler_threads.append(thread)
            thread.start()
            self._projection.pair_returned.set()
            return SimpleNamespace(status=self._projection.pair_status)
        self._handler(self, args)
        if self._projection.return_before_deferral:
            return SimpleNamespace(status=self._projection.pair_status)
        await asyncio.wait_for(
            self._projection.deferral_completed.wait(),
            timeout=0.5,
        )
        if self._projection.block_pairing:
            await self._projection.pairing_blocker.wait()
        if self._projection.pair_status == self._projection.PAIRED:
            self._projection.pairing.is_paired = True
        return SimpleNamespace(status=self._projection.pair_status)


class FakePairing:
    def __init__(self, projection: "FakeProjection", is_paired: bool) -> None:
        self._projection = projection
        self.is_paired = is_paired
        self.can_pair = True
        self.custom = FakeCustomPairing(projection)

    async def unpair_async(self):
        self._projection.unpair_calls += 1
        if self._projection.unpair_status == self._projection.UNPAIRED:
            self.is_paired = False
        return SimpleNamespace(status=self._projection.unpair_status)


class FakeDeviceInformation:
    def __init__(self, projection: "FakeProjection") -> None:
        self.id = "fake-device-id"
        self.name = "M5-Usage-D86A"
        self.pairing = projection.pairing


class FakeBluetoothDevice:
    def __init__(self, projection: "FakeProjection") -> None:
        self.device_information = FakeDeviceInformation(projection)
        self.name = "M5-Usage-D86A"
        self._projection = projection

    def close(self) -> None:
        self._projection.close_calls += 1


class FakeProjection:
    PROVIDE_PIN = "provide-pin"
    CONFIRM_ONLY = "confirm-only"
    ENCRYPTION_AND_AUTHENTICATION = "encryption-and-authentication"
    PAIRED = "paired"
    ALREADY_PAIRED = "already-paired"
    OPERATION_ALREADY_IN_PROGRESS = "operation-already-in-progress"
    FAILED = "failed"
    UNPAIRED = "unpaired"
    ALREADY_UNPAIRED = "already-unpaired"
    UNPAIR_FAILED = "unpair-failed"

    def __init__(self, *, is_paired: bool) -> None:
        self.opened_addresses: list[int] = []
        self.refresh_ids: list[str] = []
        self.close_calls = 0
        self.handler_adds = 0
        self.handler_removes = 0
        self.pair_calls: list[tuple[object, object]] = []
        self.unpair_calls = 0
        self.accepted_pins: list[str] = []
        self.completed_deferrals = 0
        self.deferral_completed = asyncio.Event()
        self.requested_kind: object | None = None
        self.pair_status = self.PAIRED
        self.refresh_pairing_states: list[bool] = []
        self.unpair_status = self.UNPAIRED
        self.return_device = True
        self.block_pairing = False
        self.return_before_deferral = False
        self.threaded_handler = False
        self.handler_threads: list[threading.Thread] = []
        self.pair_returned = threading.Event()
        self.pairing_blocker = asyncio.Event()
        self.pairing = FakePairing(self, is_paired)
        self.last_operation: FakeWinRtOperation | None = None

        projection = self

        class BluetoothLEDevice:
            @classmethod
            async def from_bluetooth_address_async(cls, address: int):
                projection.opened_addresses.append(address)
                if not projection.return_device:
                    return None
                return FakeBluetoothDevice(projection)

        class DeviceInformation:
            @classmethod
            async def create_from_id_async(cls, device_id: str):
                projection.refresh_ids.append(device_id)
                if projection.refresh_pairing_states:
                    projection.pairing.is_paired = (
                        projection.refresh_pairing_states.pop(0)
                    )
                return FakeDeviceInformation(projection)

        self.BluetoothLEDevice = BluetoothLEDevice
        self.DeviceInformation = DeviceInformation

    def load(self):
        return SimpleNamespace(
            BluetoothLEDevice=self.BluetoothLEDevice,
            DeviceInformation=self.DeviceInformation,
            DevicePairingKinds=SimpleNamespace(
                PROVIDE_PIN=self.PROVIDE_PIN,
            ),
            DevicePairingProtectionLevel=SimpleNamespace(
                ENCRYPTION_AND_AUTHENTICATION=self.ENCRYPTION_AND_AUTHENTICATION,
            ),
            DevicePairingResultStatus=SimpleNamespace(
                PAIRED=self.PAIRED,
                ALREADY_PAIRED=self.ALREADY_PAIRED,
                OPERATION_ALREADY_IN_PROGRESS=(
                    self.OPERATION_ALREADY_IN_PROGRESS
                ),
                FAILED=self.FAILED,
            ),
            DeviceUnpairingResultStatus=SimpleNamespace(
                UNPAIRED=self.UNPAIRED,
                ALREADY_UNPAIRED=self.ALREADY_UNPAIRED,
            ),
        )


class PinValidationTests(unittest.TestCase):
    def test_accepts_exactly_six_ascii_digits(self) -> None:
        self.assertEqual(validate_pin("042731"), "042731")

    def test_rejects_malformed_values_without_echoing_them(self) -> None:
        for candidate in ("12345", "1234567", "12 456", "１２３４５６", "abcdef"):
            with self.subTest(candidate=candidate):
                with self.assertRaises(PairingError) as caught:
                    validate_pin(candidate)
                self.assertNotIn(candidate, str(caught.exception))


class WindowsPairerTests(unittest.IsolatedAsyncioTestCase):
    async def test_already_paired_device_skips_pin_provider(self) -> None:
        projection = FakeProjection(is_paired=True)
        prompted = False

        async def pin_provider(_name: str) -> str:
            nonlocal prompted
            prompted = True
            return "042731"

        pairer = WindowsPairer(pin_provider, bindings_loader=projection.load)

        await pairer.ensure_paired(FakeBleDevice("70:04:1D:DC:D8:6A"))

        self.assertFalse(prompted)
        self.assertEqual(projection.pair_calls, [])
        self.assertEqual(projection.opened_addresses, [0x70041DDCD86A])
        self.assertEqual(projection.close_calls, 1)

    async def test_already_paired_device_does_not_wait_for_ceremony_lock(
        self,
    ) -> None:
        projection = FakeProjection(is_paired=True)
        ceremony_lock = asyncio.Lock()
        await ceremony_lock.acquire()
        pairer = WindowsPairer(
            lambda _name: async_value("042731"),
            ceremony_lock=ceremony_lock,
            bindings_loader=projection.load,
        )

        try:
            await asyncio.wait_for(
                pairer.ensure_paired(FakeBleDevice("70:04:1D:DC:D8:6A")),
                timeout=0.1,
            )
        finally:
            ceremony_lock.release()

        self.assertEqual(projection.pair_calls, [])

    async def test_unpaired_devices_share_one_serial_ceremony_lock(self) -> None:
        first_projection = FakeProjection(is_paired=False)
        second_projection = FakeProjection(is_paired=False)
        ceremony_lock = asyncio.Lock()
        first_prompted = asyncio.Event()
        release_first = asyncio.Event()

        async def first_pin(_name: str) -> str:
            first_prompted.set()
            await release_first.wait()
            return "042731"

        first = WindowsPairer(
            first_pin,
            ceremony_lock=ceremony_lock,
            bindings_loader=first_projection.load,
        )
        second = WindowsPairer(
            lambda _name: async_value("135790"),
            ceremony_lock=ceremony_lock,
            bindings_loader=second_projection.load,
        )

        first_task = asyncio.create_task(
            first.ensure_paired(FakeBleDevice("70:04:1D:DC:D8:6A"))
        )
        await asyncio.wait_for(first_prompted.wait(), timeout=0.1)
        second_task = asyncio.create_task(
            second.ensure_paired(FakeBleDevice("70:04:1D:DC:35:12"))
        )
        await asyncio.sleep(0)
        await asyncio.sleep(0)

        self.assertEqual(second_projection.pair_calls, [])
        release_first.set()
        await asyncio.gather(first_task, second_task)
        self.assertEqual(len(first_projection.pair_calls), 1)
        self.assertEqual(len(second_projection.pair_calls), 1)

    async def test_busy_pairing_waits_for_refreshed_paired_state(self) -> None:
        projection = FakeProjection(is_paired=False)
        projection.pair_status = projection.OPERATION_ALREADY_IN_PROGRESS
        projection.refresh_pairing_states = [False, False, False, True]
        sleeps: list[float] = []

        async def record_sleep(delay: float) -> None:
            sleeps.append(delay)

        pairer = WindowsPairer(
            lambda _name: async_value("042731"),
            timeout=5,
            poll_interval=1,
            sleep=record_sleep,
            bindings_loader=projection.load,
        )

        await pairer.ensure_paired(FakeBleDevice("70:04:1D:DC:D8:6A"))

        self.assertEqual(len(projection.pair_calls), 1)
        self.assertEqual(sleeps, [1, 1])
        self.assertTrue(projection.pairing.is_paired)

    async def test_busy_timeout_has_actionable_sanitized_error(self) -> None:
        projection = FakeProjection(is_paired=False)
        projection.pair_status = projection.OPERATION_ALREADY_IN_PROGRESS
        projection.refresh_pairing_states = [False, False, False]
        clock_values = iter((0.0, 5.0))
        pairer = WindowsPairer(
            lambda _name: async_value("042731"),
            timeout=5,
            poll_interval=1,
            sleep=asyncio.sleep,
            clock=lambda: next(clock_values),
            bindings_loader=projection.load,
        )

        with self.assertRaises(PairingBusyError) as caught:
            await pairer.ensure_paired(FakeBleDevice("70:04:1D:DC:D8:6A"))

        message = str(caught.exception)
        self.assertIn("M5-Usage-D86A", message)
        self.assertIn("Add a device", message)
        self.assertIn("scoped pairing repair", message)
        self.assertNotIn("042731", message)
        self.assertEqual(len(projection.pair_calls), 1)

    async def test_busy_poll_releases_ceremony_lock(self) -> None:
        first_projection = FakeProjection(is_paired=False)
        first_projection.pair_status = (
            first_projection.OPERATION_ALREADY_IN_PROGRESS
        )
        second_projection = FakeProjection(is_paired=False)
        ceremony_lock = asyncio.Lock()
        polling_started = asyncio.Event()
        release_poll = asyncio.Event()
        second_prompted = asyncio.Event()

        async def blocked_poll(_delay: float) -> None:
            polling_started.set()
            await release_poll.wait()

        async def second_pin(_name: str) -> str:
            second_prompted.set()
            return "135790"

        first = WindowsPairer(
            lambda _name: async_value("042731"),
            ceremony_lock=ceremony_lock,
            poll_interval=1,
            sleep=blocked_poll,
            bindings_loader=first_projection.load,
        )
        second = WindowsPairer(
            second_pin,
            ceremony_lock=ceremony_lock,
            bindings_loader=second_projection.load,
        )

        first_task = asyncio.create_task(
            first.ensure_paired(FakeBleDevice("70:04:1D:DC:D8:6A"))
        )
        await asyncio.wait_for(polling_started.wait(), timeout=0.1)
        second_task = asyncio.create_task(
            second.ensure_paired(FakeBleDevice("70:04:1D:DC:35:12"))
        )
        await asyncio.wait_for(second_prompted.wait(), timeout=0.1)
        first_projection.pairing.is_paired = True
        release_poll.set()
        await asyncio.gather(first_task, second_task)

        self.assertEqual(len(first_projection.pair_calls), 1)
        self.assertEqual(len(second_projection.pair_calls), 1)

    async def test_unpaired_device_uses_provide_pin_and_authenticated_encryption(
        self,
    ) -> None:
        projection = FakeProjection(is_paired=False)
        pairer = WindowsPairer(
            lambda _name: async_value("042731"),
            bindings_loader=projection.load,
        )

        await pairer.ensure_paired(FakeBleDevice("70:04:1D:DC:D8:6A"))

        self.assertEqual(
            projection.pair_calls,
            [(projection.PROVIDE_PIN, projection.ENCRYPTION_AND_AUTHENTICATION)],
        )
        self.assertEqual(projection.accepted_pins, ["042731"])
        self.assertEqual(projection.completed_deferrals, 1)
        self.assertEqual(projection.handler_adds, 1)
        self.assertEqual(projection.handler_removes, 1)

    async def test_repair_unpairs_once_then_reuses_new_bond(self) -> None:
        projection = FakeProjection(is_paired=True)
        pairer = WindowsPairer(
            lambda _name: async_value("042731"),
            repair=True,
            bindings_loader=projection.load,
        )
        device = FakeBleDevice("70:04:1D:DC:D8:6A")

        await pairer.ensure_paired(device)
        await pairer.ensure_paired(device)

        self.assertEqual(projection.unpair_calls, 1)
        self.assertEqual(len(projection.pair_calls), 1)
        self.assertGreaterEqual(len(projection.refresh_ids), 2)

    async def test_invalid_pin_completes_deferral_and_is_not_leaked(self) -> None:
        projection = FakeProjection(is_paired=False)
        invalid_pin = "private-pin-value"
        pairer = WindowsPairer(
            lambda _name: async_value(invalid_pin),
            bindings_loader=projection.load,
        )

        with self.assertRaises(PairingError) as caught:
            await pairer.ensure_paired(FakeBleDevice("70:04:1D:DC:D8:6A"))

        self.assertEqual(projection.completed_deferrals, 1)
        self.assertEqual(projection.accepted_pins, [])
        self.assertEqual(projection.handler_removes, 1)
        self.assertNotIn(invalid_pin, str(caught.exception))

    async def test_unsupported_pairing_request_is_rejected(self) -> None:
        projection = FakeProjection(is_paired=False)
        projection.requested_kind = projection.CONFIRM_ONLY
        pairer = WindowsPairer(
            lambda _name: async_value("042731"),
            bindings_loader=projection.load,
        )

        with self.assertRaisesRegex(PairingError, "unsupported pairing ceremony"):
            await pairer.ensure_paired(FakeBleDevice("70:04:1D:DC:D8:6A"))

        self.assertEqual(projection.completed_deferrals, 1)
        self.assertEqual(projection.accepted_pins, [])

    async def test_failed_pairing_result_is_sanitized(self) -> None:
        projection = FakeProjection(is_paired=False)
        projection.pair_status = projection.FAILED
        pairer = WindowsPairer(
            lambda _name: async_value("042731"),
            bindings_loader=projection.load,
        )

        with self.assertLogs(
            "quotaframe_bridge.pairing.windows", level="WARNING"
        ) as captured:
            with self.assertRaisesRegex(PairingError, "Windows pairing failed"):
                await pairer.ensure_paired(
                    FakeBleDevice("70:04:1D:DC:D8:6A")
                )

        self.assertIn("failed", captured.output[0].lower())
        self.assertNotIn("042731", captured.output[0])

    async def test_failed_unpair_result_is_sanitized(self) -> None:
        projection = FakeProjection(is_paired=True)
        projection.unpair_status = projection.UNPAIR_FAILED
        pairer = WindowsPairer(
            lambda _name: async_value("042731"),
            repair=True,
            bindings_loader=projection.load,
        )

        with self.assertRaisesRegex(PairingError, "Windows unpair failed"):
            await pairer.ensure_paired(FakeBleDevice("70:04:1D:DC:D8:6A"))

        self.assertEqual(projection.pair_calls, [])

    async def test_missing_windows_device_is_reported(self) -> None:
        projection = FakeProjection(is_paired=False)
        projection.return_device = False
        pairer = WindowsPairer(
            lambda _name: async_value("042731"),
            bindings_loader=projection.load,
        )

        with self.assertRaisesRegex(PairingError, "Windows BLE device"):
            await pairer.ensure_paired(FakeBleDevice("70:04:1D:DC:D8:6A"))

    async def test_malformed_ble_address_is_rejected(self) -> None:
        projection = FakeProjection(is_paired=False)
        pairer = WindowsPairer(
            lambda _name: async_value("042731"),
            bindings_loader=projection.load,
        )

        with self.assertRaisesRegex(PairingError, "Bluetooth address"):
            await pairer.ensure_paired(FakeBleDevice("not-an-address"))

        self.assertEqual(projection.opened_addresses, [])

    async def test_pairing_timeout_is_sanitized(self) -> None:
        projection = FakeProjection(is_paired=False)
        projection.block_pairing = True
        pairer = WindowsPairer(
            lambda _name: async_value("042731"),
            timeout=0.01,
            bindings_loader=projection.load,
        )

        with self.assertRaisesRegex(PairingError, "Windows pairing timed out"):
            await pairer.ensure_paired(FakeBleDevice("70:04:1D:DC:D8:6A"))

        self.assertEqual(projection.handler_removes, 1)

    async def test_cancellation_cancels_owned_winrt_operation(self) -> None:
        projection = FakeProjection(is_paired=False)
        projection.block_pairing = True
        pairer = WindowsPairer(
            lambda _name: async_value("042731"),
            bindings_loader=projection.load,
        )

        pairing = asyncio.create_task(
            pairer.ensure_paired(FakeBleDevice("70:04:1D:DC:D8:6A"))
        )
        await asyncio.wait_for(projection.deferral_completed.wait(), timeout=0.1)
        pairing.cancel()
        with self.assertRaises(asyncio.CancelledError):
            await pairing

        self.assertIsNotNone(projection.last_operation)
        assert projection.last_operation is not None
        self.assertEqual(projection.last_operation.cancel_calls, 1)
        self.assertEqual(projection.handler_removes, 1)

    async def test_immediate_platform_failure_cancels_queued_pin_request(
        self,
    ) -> None:
        projection = FakeProjection(is_paired=False)
        projection.return_before_deferral = True
        projection.pair_status = projection.FAILED
        prompts = 0

        async def pin_provider(_name: str) -> str:
            nonlocal prompts
            prompts += 1
            return "042731"

        pairer = WindowsPairer(
            pin_provider,
            bindings_loader=projection.load,
        )

        with self.assertRaisesRegex(PairingError, "Windows pairing failed"):
            await pairer.ensure_paired(FakeBleDevice("70:04:1D:DC:D8:6A"))
        await asyncio.sleep(0)
        await asyncio.sleep(0)

        self.assertEqual(prompts, 0)
        self.assertEqual(projection.accepted_pins, [])
        self.assertEqual(projection.completed_deferrals, 1)

    async def test_cleanup_waits_for_cross_thread_request_registration(
        self,
    ) -> None:
        projection = FakeProjection(is_paired=False)
        projection.threaded_handler = True
        projection.pair_status = projection.FAILED
        published = threading.Event()
        release_registration = threading.Event()
        real_submit = asyncio.run_coroutine_threadsafe

        async def pin_provider(_name: str) -> str:
            await asyncio.Event().wait()
            return "042731"

        def delayed_submit(coroutine, loop):
            future = real_submit(coroutine, loop)
            published.set()
            release_registration.wait(timeout=0.5)
            return future

        pairer = WindowsPairer(
            pin_provider,
            bindings_loader=projection.load,
        )

        with patch(
            "quotaframe_bridge.pairing.windows.asyncio.run_coroutine_threadsafe",
            side_effect=delayed_submit,
        ):
            pairing = asyncio.create_task(
                pairer.ensure_paired(
                    FakeBleDevice("70:04:1D:DC:D8:6A")
                )
            )
            self.assertTrue(await asyncio.to_thread(published.wait, 0.5))
            self.assertTrue(
                await asyncio.to_thread(projection.pair_returned.wait, 0.5)
            )
            await asyncio.sleep(0)
            await asyncio.sleep(0)
            returned_before_registration = pairing.done()
            release_registration.set()

            with self.assertRaisesRegex(PairingError, "Windows pairing failed"):
                await pairing

        for thread in projection.handler_threads:
            thread.join(timeout=0.5)
        self.assertFalse(returned_before_registration)
        self.assertEqual(projection.accepted_pins, [])
        self.assertEqual(projection.completed_deferrals, 1)

class RepairRequestTests(unittest.TestCase):
    async def _pin(self, device_name: str) -> str:
        return "123456"

    def test_repair_defaults_to_false(self) -> None:
        pairer = WindowsPairer(self._pin)
        self.assertFalse(pairer.consume_repair())

    def test_request_repair_is_consumed_once(self) -> None:
        pairer = WindowsPairer(self._pin)
        pairer.request_repair()
        self.assertTrue(pairer.consume_repair())
        self.assertFalse(pairer.consume_repair())

    def test_constructor_repair_still_honoured_once(self) -> None:
        pairer = WindowsPairer(self._pin, repair=True)
        self.assertTrue(pairer.consume_repair())
        self.assertFalse(pairer.consume_repair())


if __name__ == "__main__":
    unittest.main()
