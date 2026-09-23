"""Receive wheel events only over this process's notification icon."""

from __future__ import annotations

import ctypes
from ctypes import wintypes
import logging
from collections.abc import Callable

from quotaframe_bridge.ui.wheel import WheelSteps

LOGGER = logging.getLogger(__name__)
WM_MOUSEWHEEL = 0x020A
WH_MOUSE_LL = 14


class _IconIdentifier(ctypes.Structure):
    _fields_ = [("cbSize", wintypes.DWORD), ("hWnd", wintypes.HWND),
                ("uID", wintypes.UINT), ("guidItem", ctypes.c_byte * 16)]


class _MouseEvent(ctypes.Structure):
    _fields_ = [("pt", wintypes.POINT), ("mouseData", wintypes.DWORD),
                ("flags", wintypes.DWORD), ("time", wintypes.DWORD),
                ("extra", ctypes.c_size_t)]


class TrayWheelHook:
    """Install and remove on the tray's message-loop thread.

    The callback only hit-tests and posts a message; BLE work runs elsewhere.
    Events outside the icon always continue through the Windows hook chain.
    """

    def __init__(self, window: Callable[[], int | None], icon_id: int,
                 message: int) -> None:
        self._window = window
        self._icon_id = icon_id & 0xFFFFFFFF
        self._message = message
        self._hook = None
        self._steps = WheelSteps()
        self._user = ctypes.WinDLL("user32", use_last_error=True)
        self._shell = ctypes.WinDLL("shell32", use_last_error=True)
        self._proc_type = ctypes.WINFUNCTYPE(ctypes.c_ssize_t, ctypes.c_int,
                                           ctypes.c_size_t, ctypes.c_ssize_t)
        self._proc = self._proc_type(self._callback)
        self._user.SetWindowsHookExW.argtypes = [ctypes.c_int, self._proc_type,
                                                wintypes.HINSTANCE, wintypes.DWORD]
        self._user.SetWindowsHookExW.restype = wintypes.HANDLE
        self._user.CallNextHookEx.argtypes = [wintypes.HANDLE, ctypes.c_int,
                                            ctypes.c_size_t, ctypes.c_ssize_t]
        self._user.CallNextHookEx.restype = ctypes.c_ssize_t
        self._user.UnhookWindowsHookEx.argtypes = [wintypes.HANDLE]
        self._user.UnhookWindowsHookEx.restype = wintypes.BOOL
        self._user.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT,
                                          ctypes.c_size_t, ctypes.c_ssize_t]
        self._user.PostMessageW.restype = wintypes.BOOL
        self._shell.Shell_NotifyIconGetRect.argtypes = [ctypes.POINTER(_IconIdentifier),
                                                       ctypes.POINTER(wintypes.RECT)]
        self._shell.Shell_NotifyIconGetRect.restype = ctypes.c_long

    def start(self) -> None:
        self._hook = self._user.SetWindowsHookExW(WH_MOUSE_LL, self._proc, None, 0)
        if not self._hook:
            LOGGER.warning("Tray wheel unavailable: Windows error %s", ctypes.get_last_error())

    def stop(self) -> None:
        if self._hook:
            self._user.UnhookWindowsHookEx(self._hook)
            self._hook = None

    def _callback(self, code: int, message: int, pointer: int) -> int:
        try:
            if code >= 0 and message == WM_MOUSEWHEEL:
                hwnd = self._window()
                if hwnd:
                    event = ctypes.cast(pointer, ctypes.POINTER(_MouseEvent)).contents
                    identity = _IconIdentifier(cbSize=ctypes.sizeof(_IconIdentifier),
                                               hWnd=hwnd, uID=self._icon_id)
                    rect = wintypes.RECT()
                    found = self._shell.Shell_NotifyIconGetRect(ctypes.byref(identity), ctypes.byref(rect)) == 0
                    if found and rect.left <= event.pt.x < rect.right and rect.top <= event.pt.y < rect.bottom:
                        delta = ctypes.c_short(event.mouseData >> 16).value
                        direction = self._steps.feed(delta)
                        if direction:
                            self._user.PostMessageW(hwnd, self._message, int(direction > 0), 0)
                        return 1
                    self._steps.reset()
        except Exception:
            LOGGER.exception("Tray wheel event failed")
        return self._user.CallNextHookEx(self._hook, code, message, pointer)
