from __future__ import annotations

import ctypes
import unittest
from types import SimpleNamespace

from quotaframe_bridge.ui.wheel import WheelSteps
from quotaframe_bridge.ui.windows_wheel import TrayWheelHook, _MouseEvent, WM_MOUSEWHEEL


class WheelTests(unittest.TestCase):
    def test_partial_steps_direction_and_burst_limit(self):
        now = [0.0]
        steps = WheelSteps(lambda: now[0])
        self.assertEqual(steps.feed(60), 0)
        self.assertEqual(steps.feed(60), -1)
        self.assertEqual(steps.feed(-120), 0)
        now[0] = 0.3
        self.assertEqual(steps.feed(-120), 1)
        now[0] = 1
        self.assertEqual(steps.feed(60), 0)
        now[0] = 2
        self.assertEqual(steps.feed(60), 0)
        self.assertEqual(steps.feed(60), -1)

    def test_native_hook_only_consumes_our_icon_wheel(self):
        hook = TrayWheelHook.__new__(TrayWheelHook)
        hook._window = lambda: 1
        hook._icon_id = 17
        hook._message = 1000
        hook._hook = None
        hook._steps = WheelSteps()
        posted = []
        hook._user = SimpleNamespace(
            PostMessageW=lambda *args: posted.append(args),
            CallNextHookEx=lambda *args: 99,
        )
        def get_rect(identity, rectangle):
            self.assertEqual(identity._obj.uID, 17)
            rectangle._obj.left, rectangle._obj.top = 10, 20
            rectangle._obj.right, rectangle._obj.bottom = 30, 40
            return 0
        hook._shell = SimpleNamespace(Shell_NotifyIconGetRect=get_rect)
        event = _MouseEvent()
        event.pt.x, event.pt.y = 15, 25
        event.mouseData = 120 << 16
        self.assertEqual(hook._callback(0, WM_MOUSEWHEEL, ctypes.addressof(event)), 1)
        self.assertEqual(posted, [(1, 1000, 0, 0)])
        event.pt.x = 30
        self.assertEqual(hook._callback(0, WM_MOUSEWHEEL, ctypes.addressof(event)), 99)
        self.assertEqual(hook._callback(-1, WM_MOUSEWHEEL, 0), 99)
        self.assertEqual(hook._callback(0, 0, 0), 99)
        hook._shell.Shell_NotifyIconGetRect = lambda *_: -1
        event.pt.x = 15
        self.assertEqual(hook._callback(0, WM_MOUSEWHEEL, ctypes.addressof(event)), 99)
        self.assertEqual(len(posted), 1)
