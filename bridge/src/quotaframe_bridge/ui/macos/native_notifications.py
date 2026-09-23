"""Late-bound macOS Notification Center delivery."""

from __future__ import annotations

import logging
import threading
import uuid
from collections.abc import Callable
from dataclasses import dataclass
from typing import Any


LOGGER = logging.getLogger(__name__)


class NotificationCenterError(RuntimeError):
    pass


@dataclass(frozen=True)
class NotificationBindings:
    center: Any
    make_content: Callable[[], Any]
    make_request: Callable[[str, Any], Any]
    dispatch_to_main: Callable[[Callable[[], None]], None]
    authorization_options: int
    not_determined: int
    denied: int
    authorized: int
    provisional: int


def load_notification_bindings() -> NotificationBindings:
    try:
        import AppKit
        import UserNotifications
    except ImportError as exc:
        raise NotificationCenterError(
            "macOS UserNotifications framework is unavailable"
        ) from exc

    return NotificationBindings(
        center=UserNotifications.UNUserNotificationCenter.currentNotificationCenter(),
        make_content=UserNotifications.UNMutableNotificationContent.alloc().init,
        make_request=lambda identifier, content: (
            UserNotifications.UNNotificationRequest.requestWithIdentifier_content_trigger_(
                identifier,
                content,
                None,
            )
        ),
        dispatch_to_main=lambda work: (
            AppKit.NSOperationQueue.mainQueue().addOperationWithBlock_(work)
        ),
        authorization_options=UserNotifications.UNAuthorizationOptionAlert,
        not_determined=UserNotifications.UNAuthorizationStatusNotDetermined,
        denied=UserNotifications.UNAuthorizationStatusDenied,
        authorized=UserNotifications.UNAuthorizationStatusAuthorized,
        provisional=UserNotifications.UNAuthorizationStatusProvisional,
    )


class MacNotificationCenter:
    def __init__(self, bindings: NotificationBindings | None = None) -> None:
        self._bindings = bindings
        self._authorization_requested = False
        self._lock = threading.Lock()

    def notify(self, title: str, body: str) -> bool:
        """Schedule one notification, returning false on synchronous rejection."""

        try:
            bindings = (
                load_notification_bindings()
                if self._bindings is None
                else self._bindings
            )
            self._bindings = bindings
        except Exception as exc:
            self._log_failure("framework", exc)
            return False

        accepted = [True]

        def fail(stage: str, error: object | None = None) -> None:
            accepted[0] = False
            self._log_failure(stage, error)

        def dispatch(stage: str, work: Callable[[], None]) -> None:
            try:
                bindings.dispatch_to_main(work)
            except Exception as exc:
                fail(stage, exc)

        def delivery_finished(error: object | None) -> None:
            if error is not None:
                fail("delivery", error)

        def deliver() -> None:
            def work() -> None:
                try:
                    content = bindings.make_content()
                    content.setTitle_(title)
                    content.setBody_(body)
                    request = bindings.make_request(uuid.uuid4().hex, content)
                    bindings.center.addNotificationRequest_withCompletionHandler_(
                        request,
                        delivery_finished,
                    )
                except Exception as exc:
                    fail("delivery", exc)

            dispatch("delivery", work)

        def authorization_finished(
            granted: bool,
            error: object | None,
        ) -> None:
            if error is not None or not granted:
                fail("permission", error)
                return
            deliver()

        def request_authorization() -> None:
            with self._lock:
                if self._authorization_requested:
                    fail("permission")
                    return
                self._authorization_requested = True

            def work() -> None:
                try:
                    bindings.center.requestAuthorizationWithOptions_completionHandler_(
                        bindings.authorization_options,
                        authorization_finished,
                    )
                except Exception as exc:
                    fail("permission", exc)

            dispatch("permission", work)

        def settings_finished(settings: object) -> None:
            try:
                status = settings.authorizationStatus()  # type: ignore[attr-defined]
            except Exception as exc:
                fail("settings", exc)
                return
            if status in {bindings.authorized, bindings.provisional}:
                deliver()
            elif status == bindings.not_determined:
                request_authorization()
            else:
                fail("permission")

        def begin() -> None:
            try:
                bindings.center.getNotificationSettingsWithCompletionHandler_(
                    settings_finished
                )
            except Exception as exc:
                fail("settings", exc)

        dispatch("settings", begin)
        return accepted[0]

    @staticmethod
    def _log_failure(stage: str, _error: object | None = None) -> None:
        LOGGER.warning("macOS notification unavailable at %s stage", stage)
