"""Tray-side presentation of the Bridge self-update state.

Shared by both tray entry points. The Windows tray owns a dedicated UI thread
and passes a dispatch that marshals onto it; the macOS shell already hands
every AppKit mutation to the main queue itself, so it dispatches inline.
"""

from __future__ import annotations

import logging
from collections.abc import Callable

from quotaframe_bridge.i18n import tr
from quotaframe_bridge.ui.controller import TrayController
from quotaframe_bridge.service.update import UpdateState

LOGGER = logging.getLogger(__name__)

BrowserOpen = Callable[[str], object]
Dispatch = Callable[[Callable[[], None]], None]


def run_inline(action: Callable[[], None]) -> None:
    """Dispatch for a shell that already marshals onto its own UI thread."""

    action()


class UpdatePresenter:
    def __init__(
        self,
        controller: TrayController,
        browser: BrowserOpen,
        *,
        dispatch: Dispatch = run_inline,
    ) -> None:
        self._controller = controller
        self._browser = browser
        self._dispatch = dispatch
        self._page_url: str | None = None

    def set_update_state(self, state: UpdateState) -> None:
        self._page_url = state.page_url
        self._dispatch(
            lambda: self._controller.set_bridge_update_action(
                state.enabled, state.detail
            )
        )

    def notify(self, title: str, body: str) -> None:
        self._dispatch(lambda: self._controller.notify(title, body))

    def open_page(self) -> bool:
        page_url = self._page_url
        if page_url is None:
            return False
        try:
            opened = self._browser(page_url)
        except Exception as exc:
            LOGGER.warning("release page browser failed: %s", type(exc).__name__)
            opened = False
        if not opened:
            self.notify(tr("open_download_failed"), tr("retry_later"))
        return True
