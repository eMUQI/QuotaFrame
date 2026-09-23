"""Event routing, scroll scope, and lifecycle without a live menu bar."""

import unittest
from types import SimpleNamespace
from unittest.mock import Mock

from quotaframe_bridge.ui.macos.interactions import StatusItemInteractions
from quotaframe_bridge.ui.wheel import WheelSteps


class InteractionTests(unittest.TestCase):
    def setUp(self):
        self.window = Mock()
        self.window.isVisible.return_value = True
        self.window.convertPointFromScreen_.side_effect = lambda p: (p[0] - 100, p[1] - 200)
        self.button = Mock()
        self.button.window.return_value = self.window
        self.button.convertPoint_fromView_.side_effect = lambda point, view: point
        self.button.bounds.return_value = (0, 0, 20, 20)
        self.button.isHiddenOrHasHiddenAncestor.return_value = False
        self.item = Mock()
        self.item.button.return_value = self.button
        self.menu = Mock()
        self.toggle = Mock()
        self.page = Mock()
        self.app = Mock()
        self.kit = SimpleNamespace(
            NSEventMaskLeftMouseUp=1, NSEventMaskRightMouseUp=2,
            NSEventMaskScrollWheel=4, NSEventTypeLeftMouseUp=1,
            NSEventModifierFlagControl=8, NSEventPhaseNone=0,
            NSEvent=Mock(), NSApplication=Mock(),
            NSPointInRect=lambda p, rect: 0 <= p[0] < 20 and 0 <= p[1] < 20,
        )
        self.kit.NSApplication.sharedApplication.return_value = self.app
        self.actions = StatusItemInteractions(
            self.kit, self.item, self.menu, lambda handler: (handler, "invoke:"),
            self.toggle, self.page,
        )
        self.now = 1.0
        self.actions._wheel = WheelSteps(lambda: self.now)

    def event(self, *, delta=1, precise=False, momentum=0, point=(10, 10), dx=0):
        return SimpleNamespace(
            window=lambda: self.window, locationInWindow=lambda: point,
            scrollingDeltaY=lambda: delta, scrollingDeltaX=lambda: dx,
            momentumPhase=lambda: momentum, hasPreciseScrollingDeltas=lambda: precise,
        )

    def test_left_click_toggles_and_secondary_or_keyboard_action_opens_menu(self):
        self.item.setMenu_.assert_called_once_with(None)
        for kind, modifiers in [(1, 0), (2, 0), (1, 8), (10, 0)]:
            self.app.currentEvent.return_value = SimpleNamespace(
                type=lambda: kind, modifierFlags=lambda: modifiers,
            )
            self.actions._target()
        self.toggle.assert_called_once_with()
        self.assertEqual(self.menu.popUpMenuPositioningItem_atLocation_inView_.call_count, 3)
        self.button.highlight_.assert_called_with(False)

    def test_scroll_only_consumes_vertical_events_over_own_button(self):
        outside = self.event(point=(25, 5))
        horizontal = self.event(dx=4)
        momentum = self.event(momentum=1)
        other_window = self.event()
        other_window.window = lambda: object()
        for event in (outside, horizontal, momentum, other_window):
            self.assertIs(self.actions._scroll(event), event)
        self.page.assert_not_called()
        self.assertIsNone(self.actions._scroll(self.event()))
        self.page.assert_called_once_with(-1)

    def test_precise_accumulation_cooldown_and_idle_reset(self):
        for delta in (4, 4, 4):
            self.actions._scroll(self.event(delta=delta, precise=True))
        self.page.assert_called_once_with(-1)
        self.actions._scroll(self.event(delta=-20, precise=True))
        self.page.assert_called_once()
        self.now += 0.3
        self.actions._scroll(self.event(delta=-12, precise=True))
        self.assertEqual(self.page.call_args.args, (1,))
        self.page.reset_mock()
        self.now += 0.3
        self.actions._scroll(self.event(delta=8, precise=True))
        self.now += 0.6
        self.actions._scroll(self.event(delta=4, precise=True))
        self.page.assert_not_called()

    def test_windowless_scroll_uses_screen_coordinates(self):
        event = self.event(point=(110, 210))
        event.window = lambda: None
        self.assertIsNone(self.actions._scroll(event))
        self.page.assert_called_once_with(-1)
        self.window.convertPointFromScreen_.assert_called_once_with((110, 210))

    def test_global_monitor_dispatches_only_over_visible_button(self):
        register = self.kit.NSEvent.addGlobalMonitorForEventsMatchingMask_handler_
        register.assert_called_once()
        mask, handler = register.call_args.args
        self.assertEqual(mask, self.kit.NSEventMaskScrollWheel)
        event = self.event(point=(110, 210))
        event.window = lambda: None
        outside = self.event(point=(125, 210))
        outside.window = lambda: None
        self.assertIsNone(handler(outside))
        self.window.isVisible.return_value = False
        handler(event)
        self.window.isVisible.return_value = True
        self.button.isHiddenOrHasHiddenAncestor.return_value = True
        handler(event)
        self.button.isHiddenOrHasHiddenAncestor.return_value = False
        self.actions._menu_open = True
        handler(event)
        self.actions._menu_open = False
        self.page.assert_not_called()
        self.assertIsNone(handler(event))
        self.page.assert_called_once_with(-1)
        self.actions.stop()
        self.now += 1
        handler(event)
        self.page.assert_called_once()

    def test_menu_tracking_and_stop_do_not_dispatch_scroll(self):
        self.actions._menu_open = True
        event = self.event()
        self.assertIs(self.actions._scroll(event), event)
        self.actions._menu_open = False
        self.actions.stop()
        self.actions.stop()
        self.assertIs(self.actions._scroll(event), event)
        self.actions._click()
        self.assertEqual(self.kit.NSEvent.removeMonitor_.call_count, 2)
        removed = [call.args[0] for call in self.kit.NSEvent.removeMonitor_.call_args_list]
        self.assertIn(self.kit.NSEvent.addLocalMonitorForEventsMatchingMask_handler_.return_value, removed)
        self.assertIn(self.kit.NSEvent.addGlobalMonitorForEventsMatchingMask_handler_.return_value, removed)
        self.toggle.assert_not_called()
        self.page.assert_not_called()

    def test_menu_failure_restores_highlight_and_tracking(self):
        self.app.currentEvent.return_value = None
        self.menu.popUpMenuPositioningItem_atLocation_inView_.side_effect = RuntimeError()
        with self.assertRaises(RuntimeError):
            self.actions._click()
        self.assertFalse(self.actions._menu_open)
        self.button.highlight_.assert_called_with(False)
