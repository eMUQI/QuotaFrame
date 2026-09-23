from __future__ import annotations

import unittest

from quotaframe_bridge.ui.macos.native_notifications import (
    MacNotificationCenter,
    NotificationBindings,
)


NOT_DETERMINED = 0
DENIED = 1
AUTHORIZED = 2
PROVISIONAL = 3


class FakeSettings:
    def __init__(self, status: int) -> None:
        self._status = status

    def authorizationStatus(self) -> int:
        return self._status


class FakeContent:
    def __init__(self) -> None:
        self.title = ""
        self.body = ""

    def setTitle_(self, title: str) -> None:
        self.title = title

    def setBody_(self, body: str) -> None:
        self.body = body


class FakeCenter:
    def __init__(
        self,
        status: int,
        *,
        granted: bool = True,
        delivery_error: Exception | None = None,
    ) -> None:
        self.status = status
        self.granted = granted
        self.delivery_error = delivery_error
        self.authorization_requests = 0
        self.delivered: list[tuple[str, FakeContent]] = []

    def getNotificationSettingsWithCompletionHandler_(self, completion) -> None:
        completion(FakeSettings(self.status))

    def requestAuthorizationWithOptions_completionHandler_(
        self, _options: int, completion
    ) -> None:
        self.authorization_requests += 1
        if self.granted:
            self.status = AUTHORIZED
        completion(self.granted, None)

    def addNotificationRequest_withCompletionHandler_(
        self, request: tuple[str, FakeContent], completion
    ) -> None:
        if self.delivery_error is None:
            self.delivered.append(request)
        completion(self.delivery_error)


def bindings(center: FakeCenter) -> NotificationBindings:
    return NotificationBindings(
        center=center,
        make_content=FakeContent,
        make_request=lambda identifier, content: (identifier, content),
        dispatch_to_main=lambda work: work(),
        authorization_options=5,
        not_determined=NOT_DETERMINED,
        denied=DENIED,
        authorized=AUTHORIZED,
        provisional=PROVISIONAL,
    )


class MacNotificationCenterTests(unittest.TestCase):
    def test_authorization_is_requested_only_once(self) -> None:
        center = FakeCenter(NOT_DETERMINED)
        notifications = MacNotificationCenter(bindings(center))

        self.assertTrue(notifications.notify("新版", "v0.2.0"))
        self.assertTrue(notifications.notify("新版", "v0.2.1"))

        self.assertEqual(center.authorization_requests, 1)
        self.assertEqual(len(center.delivered), 2)

    def test_authorized_and_provisional_states_deliver_title_and_body(self) -> None:
        for status in (AUTHORIZED, PROVISIONAL):
            with self.subTest(status=status):
                center = FakeCenter(status)
                notifications = MacNotificationCenter(bindings(center))

                self.assertTrue(notifications.notify("Bridge 有新版本", "v0.2.0"))

                self.assertEqual(len(center.delivered), 1)
                _identifier, content = center.delivered[0]
                self.assertEqual(content.title, "Bridge 有新版本")
                self.assertEqual(content.body, "v0.2.0")

    def test_denied_permission_logs_and_returns_false(self) -> None:
        notifications = MacNotificationCenter(bindings(FakeCenter(DENIED)))

        with self.assertLogs(
            "quotaframe_bridge.ui.macos.native_notifications",
            level="WARNING",
        ):
            delivered = notifications.notify("新版", "v0.2.0")

        self.assertFalse(delivered)

    def test_framework_and_delivery_exceptions_log_and_return_false(self) -> None:
        class BrokenCenter(FakeCenter):
            def getNotificationSettingsWithCompletionHandler_(self, completion) -> None:
                raise RuntimeError("framework detail")

        cases = (
            MacNotificationCenter(bindings(BrokenCenter(AUTHORIZED))),
            MacNotificationCenter(
                bindings(
                    FakeCenter(
                        AUTHORIZED,
                        delivery_error=RuntimeError("delivery detail"),
                    )
                )
            ),
        )
        for notifications in cases:
            with self.subTest(notifications=notifications), self.assertLogs(
                "quotaframe_bridge.ui.macos.native_notifications",
                level="WARNING",
            ) as captured:
                delivered = notifications.notify("新版", "v0.2.0")
            self.assertFalse(delivered)
            rendered = "\n".join(captured.output)
            self.assertNotIn("framework detail", rendered)
            self.assertNotIn("delivery detail", rendered)


if __name__ == "__main__":
    unittest.main()
