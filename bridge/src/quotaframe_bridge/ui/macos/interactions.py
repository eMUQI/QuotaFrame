"""Menu-bar actions and icon-scoped scroll handling on the main thread."""

from __future__ import annotations

import logging
from collections.abc import Callable
from typing import Any

from quotaframe_bridge.ui.wheel import WheelSteps

LOGGER = logging.getLogger(__name__)


class StatusItemInteractions:
    def __init__(
        self,
        appkit: Any,
        item: Any,
        menu: Any,
        make_target: Callable[[Callable[[], None]], tuple[Any, str]],
        on_toggle: Callable[[], None],
        on_page: Callable[[int], None],
    ) -> None:
        self._appkit = appkit
        self._button = item.button()
        self._menu = menu
        self._on_toggle = on_toggle
        self._on_page = on_page
        self._wheel = WheelSteps()
        self._stopped = False
        self._menu_open = False
        self._target, selector = make_target(self._click)
        self._button.setTarget_(self._target)
        self._button.setAction_(selector)
        self._button.sendActionOn_(
            appkit.NSEventMaskLeftMouseUp | appkit.NSEventMaskRightMouseUp
        )
        # An attached menu takes precedence over the button's target/action.
        item.setMenu_(None)
        self._monitor = appkit.NSEvent.addLocalMonitorForEventsMatchingMask_handler_(
            appkit.NSEventMaskScrollWheel, self._scroll
        )
        # Local and global monitors receive disjoint application event streams.
        self._global_monitor = appkit.NSEvent.addGlobalMonitorForEventsMatchingMask_handler_(
            appkit.NSEventMaskScrollWheel, self._global_scroll
        )

    def _click(self) -> None:
        if self._stopped:
            return
        event = self._appkit.NSApplication.sharedApplication().currentEvent()
        if event is None or event.type() != self._appkit.NSEventTypeLeftMouseUp or (
            event.modifierFlags() & self._appkit.NSEventModifierFlagControl
        ):
            self._menu_open = True
            self._wheel.reset()
            self._button.highlight_(True)
            try:
                self._menu.popUpMenuPositioningItem_atLocation_inView_(
                    None, (0, 0), self._button
                )
            finally:
                self._button.highlight_(False)
                self._menu_open = False
        else:
            self._on_toggle()

    def _global_scroll(self, event: Any) -> None:
        # Global monitors observe events but cannot consume or replace them.
        self._scroll(event)

    def _scroll(self, event: Any) -> Any:
        if self._stopped or self._menu_open:
            return event
        try:
            window = self._button.window()
            if (window is None or not window.isVisible()
                    or self._button.isHiddenOrHasHiddenAncestor()):
                self._wheel.reset()
                return event
            event_window = event.window()
            point = event.locationInWindow()
            if event_window is None:
                # Windowless mouse events carry AppKit screen coordinates.
                point = window.convertPointFromScreen_(point)
            elif event_window != window:
                self._wheel.reset()
                return event
            point = self._button.convertPoint_fromView_(point, None)
            if not self._appkit.NSPointInRect(point, self._button.bounds()):
                self._wheel.reset()
                return event
            if event.momentumPhase() != self._appkit.NSEventPhaseNone:
                self._wheel.reset()
                return event
            delta = event.scrollingDeltaY()
            if not delta or abs(event.scrollingDeltaX()) > abs(delta):
                self._wheel.reset()
                return event
            # Discrete wheels use line units; precise devices accumulate 12 points
            # per page. WheelSteps limits repeats to one page every 200 ms.
            normalized = delta * (10 if event.hasPreciseScrollingDeltas() else 120)
            direction = self._wheel.feed(normalized)
            if direction:
                self._on_page(direction)
            return None
        except Exception:
            LOGGER.exception("menu-bar scroll action failed")
            return event

    def stop(self) -> None:
        if self._stopped:
            return
        self._stopped = True
        if self._monitor is not None:
            self._appkit.NSEvent.removeMonitor_(self._monitor)
        if self._global_monitor is not None:
            self._appkit.NSEvent.removeMonitor_(self._global_monitor)
        self._button.setTarget_(None)
        self._button.setAction_(None)
        self._monitor = None
        self._global_monitor = None
        self._target = None
