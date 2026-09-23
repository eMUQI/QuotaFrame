from __future__ import annotations

import logging
import unittest

from quotaframe_bridge import macos_bluetooth as permission


class FakeLogger(logging.Logger):
    def __init__(self) -> None:
        super().__init__("fake")
        self.messages: list[str] = []

    def info(self, message, *args, **kwargs) -> None:  # type: ignore[override]
        self.messages.append(message % args if args else message)


class BluetoothPreflightTests(unittest.TestCase):
    def test_denied_authorization_names_the_settings_pane(self) -> None:
        with self.assertRaises(permission.BluetoothPermissionError) as raised:
            permission.preflight(reader=lambda: permission.DENIED)

        self.assertIn("System Settings", str(raised.exception))
        self.assertIn("Bluetooth", str(raised.exception))

    def test_restricted_authorization_is_also_fatal(self) -> None:
        with self.assertRaises(permission.BluetoothPermissionError):
            permission.preflight(reader=lambda: permission.RESTRICTED)

    def test_allowed_authorization_is_silent(self) -> None:
        log = FakeLogger()

        permission.preflight(reader=lambda: permission.ALLOWED, log=log)

        self.assertEqual(log.messages, [])

    def test_unknown_authorization_does_not_block_the_run(self) -> None:
        log = FakeLogger()

        permission.preflight(reader=lambda: None, log=log)

        self.assertEqual(log.messages, [])

    def test_first_run_warns_that_a_silent_exit_means_no_usage_description(
        self,
    ) -> None:
        log = FakeLogger()

        permission.preflight(reader=lambda: permission.NOT_DETERMINED, log=log)

        self.assertEqual(len(log.messages), 1)
        self.assertIn("Terminal.app", log.messages[0])

    def test_reader_never_raises_when_corebluetooth_is_absent(self) -> None:
        # Windows and Linux hosts import this module through the CLI's tests.
        self.assertIn(
            permission.read_authorization(),
            (None, 0, 1, 2, 3),
        )


if __name__ == "__main__":
    unittest.main()
