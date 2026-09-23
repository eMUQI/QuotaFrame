"""The only module that imports tkinter; policy decisions belong elsewhere."""

from __future__ import annotations

import asyncio
import concurrent.futures
import ctypes
import logging
import queue
import tkinter as tk
from collections.abc import Callable
from typing import Any
from tkinter import font as tkfont

from quotaframe_bridge.i18n import tr
from quotaframe_bridge.paths import tray_assets
from quotaframe_bridge.ui.windows_theme import DARK, LIGHT, apps_use_dark_theme

PIN_LENGTH = 6

# The copy deck specifies the dialog in pixels at the resolution it was drawn
# for. Everything derived from it is scaled to the display the user actually
# has, and the size is a floor rather than a cap: clipping the primary button
# is a worse outcome than a dialog thirty pixels taller than the drawing.
DECK_DPI = 96.0
DECK_WIDTH = 360
DECK_HEIGHT = 240

# DWMWA_USE_IMMERSIVE_DARK_MODE. Windows 10 builds before 20H1 numbered it 19,
# and the two are not interchangeable: the wrong one returns E_INVALIDARG.
DARK_TITLE_BAR = 20
DARK_TITLE_BAR_BEFORE_20H1 = 19

LOGGER = logging.getLogger(__name__)


def apply_title_bar_theme(window: tk.Toplevel, *, dark: bool) -> bool:
    """Match the native title bar to the dialog application theme."""

    try:
        # Tk's toplevel is a child of the decorated frame that owns the caption.
        frame = ctypes.windll.user32.GetParent(window.winfo_id())
        enabled = ctypes.c_int(int(dark))
        for attribute in (DARK_TITLE_BAR, DARK_TITLE_BAR_BEFORE_20H1):
            result = ctypes.windll.dwmapi.DwmSetWindowAttribute(
                frame,
                attribute,
                ctypes.byref(enabled),
                ctypes.sizeof(enabled),
            )
            if result == 0:
                return True
    except (AttributeError, OSError, tk.TclError):
        LOGGER.debug("the desktop manager refused the title bar theme")
    return False


class TkPinPrompt:
    """Show modal dialogs on the main thread and answer async callers."""

    def __init__(self, root: tk.Tk) -> None:
        self._root = root
        self._pending: queue.SimpleQueue[Callable[[], None]] = queue.SimpleQueue()
        self._dialogs: queue.SimpleQueue[Callable[[], None]] = queue.SimpleQueue()
        self._open_dialog: tk.Toplevel | None = None
        self._dialog_request: concurrent.futures.Future | None = None
        self._in_dialog = False
        self._root.after(25, self._drain_pending)

    async def ask_pin(self, device_name: str) -> str:
        return await self._on_main_thread(self._ask_pin, device_name)

    def call_soon(self, function: Callable[..., Any], *args: object) -> None:
        """Queue non-dialog Tk work from a tray or worker thread."""

        self._pending.put(lambda: function(*args))

    def close_dialogs(self) -> None:
        """Destroy the open dialog so its nested wait_window loop can return.

        Tk's ``quit`` only unwinds the outermost loop, so Exit would otherwise
        be stranded behind whatever dialog happens to be on screen.
        """

        window = self._open_dialog
        self._open_dialog = None
        if window is None:
            return
        try:
            window.destroy()
        except tk.TclError:
            LOGGER.debug("dialog was already gone when Exit ran")

    async def confirm_repair(self, device_name: str) -> bool:
        return await self._on_main_thread(
            self._confirm,
            tr("repair_device", device_name=device_name),
            tr("repair_body"),
            tr("repair"),
        )

    async def confirm_firmware(self, body: str) -> bool:
        return await self._on_main_thread(self._confirm, tr("check_and_update"), body, tr("ok"))

    async def confirm_forget(self, device_name: str) -> bool:
        return await self._on_main_thread(
            self._confirm,
            tr("forget_device", device_name=device_name),
            tr("forget_body"),
            tr("forget"),
        )

    def show_already_running(self) -> None:
        self._confirm(
            tr("already_running"),
            tr("already_running_windows"),
            tr("ok"),
            cancel=False,
        )

    async def _on_main_thread(self, function, *args):  # type: ignore[no-untyped-def]
        future: concurrent.futures.Future = concurrent.futures.Future()
        # Dialogs queue separately from plain callbacks: they block for as long
        # as the user looks at them, and only one may be on screen at a time.
        self._dialogs.put(lambda: self._resolve(future, function, args))
        try:
            return await asyncio.wrap_future(future)
        except asyncio.CancelledError:
            future.cancel()
            self.call_soon(self._close_cancelled_dialog, future)
            raise

    def _close_cancelled_dialog(self, future: concurrent.futures.Future) -> None:
        """Close only the window owned by this request, on the Tk thread."""
        if self._dialog_request is future:
            self.close_dialogs()

    def _resolve(
        self,
        future: concurrent.futures.Future,
        function: Callable[..., object],
        args: tuple[object, ...],
    ) -> None:
        if future.set_running_or_notify_cancel():
            self._dialog_request = future
            try:
                future.set_result(function(*args))
            except Exception as exc:  # noqa: BLE001 - marshalled to caller
                future.set_exception(exc)
            finally:
                self._dialog_request = None

    def _drain_pending(self) -> None:
        """Run queued Tk work on Tk's owning thread.

        Re-arming comes first on purpose. A dialog blocks this call inside
        ``wait_window``, whose nested loop still services timers that were
        already armed but would never see one armed after the dialog closes.
        That is what keeps status updates and Exit alive during a prompt.
        """

        self._root.after(25, self._drain_pending)

        while True:
            try:
                callback = self._pending.get_nowait()
            except queue.Empty:
                break
            try:
                callback()
            except Exception:
                # Status rendering must not silently kill the only path used
                # by pairing prompts and the Exit command.
                LOGGER.exception("Tk dispatch callback failed")

        if self._in_dialog:
            return
        try:
            dialog = self._dialogs.get_nowait()
        except queue.Empty:
            return
        self._in_dialog = True
        try:
            dialog()
        finally:
            self._in_dialog = False

    def _window(self, title: str) -> tk.Toplevel:
        dark = apps_use_dark_theme()
        self._colors = DARK if dark else LIGHT
        window = tk.Toplevel(self._root)
        window.title(title)
        window.iconbitmap(str(tray_assets() / "quotaframe-bridge.ico"))
        window.configure(bg=self._colors.bg)
        window.resizable(False, False)
        # A tray process's dialog is otherwise pushed behind the foreground
        # window and only blinks in the taskbar, which reads as nothing
        # having happened.
        window.attributes("-topmost", True)
        # Deliberately not transient: Tk gives a transient window its master's
        # state when it maps, and the root is withdrawn, so the dialog would be
        # created withdrawn and wait_window would block on a window nobody can
        # see. The taskbar button a plain toplevel gets is worth having anyway.
        window.grab_set()
        # Apply the native frame preference before displaying the content.
        window.update_idletasks()
        apply_title_bar_theme(window, dark=dark)
        self._open_dialog = window
        return window

    def _wait(self, window: tk.Toplevel) -> None:
        """Show the dialog and block until the user or Exit closes it."""

        self._center(window)
        try:
            self._root.wait_window(window)
        finally:
            self._open_dialog = None

    def _px(self, window: tk.Misc, deck_pixels: int) -> int:
        """Scale a logical measurement to this display's resolution."""

        return round(deck_pixels * window.winfo_fpixels("1i") / DECK_DPI)

    def _center(self, window: tk.Toplevel) -> None:
        window.update_idletasks()
        width = max(self._px(window, DECK_WIDTH), window.winfo_reqwidth())
        height = max(self._px(window, DECK_HEIGHT), window.winfo_reqheight())
        x = (window.winfo_screenwidth() - width) // 2
        y = (window.winfo_screenheight() - height) // 2
        window.geometry(f"{width}x{height}+{x}+{y}")

    def _ask_pin(self, device_name: str) -> str:
        window = self._window(tr("pair_panel"))
        result: dict[str, str] = {}
        pad = self._px(window, 24)
        gap = self._px(window, 8)

        title_font = tkfont.Font(family="Microsoft YaHei UI", size=15, weight="bold")
        body_font = tkfont.Font(family="Microsoft YaHei UI", size=10)

        tk.Label(
            window,
            text=tr("enter_pin"),
            bg=self._colors.bg,
            fg=self._colors.text,
            font=title_font,
        ).pack(padx=pad, pady=(pad, gap), anchor="w")

        tk.Label(
            window,
            text=tr("pin_instructions", device_name=device_name),
            bg=self._colors.bg,
            fg=self._colors.muted,
            font=body_font,
            wraplength=self._px(window, 312),
            justify="left",
            height=2,
        ).pack(padx=pad, anchor="w")

        entry_value = tk.StringVar()
        entry = tk.Entry(
            window,
            textvariable=entry_value,
            bg=self._colors.panel,
            fg=self._colors.text,
            insertbackground=self._colors.text,
            selectbackground=self._colors.text,
            selectforeground=self._colors.bg,
            relief="flat",
            justify="center",
            font=title_font,
        )
        entry.pack(padx=pad, pady=self._px(window, 12), fill="x", ipady=gap)
        entry.focus_set()

        # Reserve the error row up front so showing it never shifts anything.
        error = tk.Label(
            window,
            text="",
            bg=self._colors.bg,
            fg=self._colors.error,
            font=body_font,
            height=1,
        )
        error.pack(padx=pad, anchor="w")

        buttons = tk.Frame(window, bg=self._colors.bg)
        buttons.pack(padx=pad, pady=(gap, pad), fill="x")

        def submit() -> None:
            candidate = entry_value.get()
            if len(candidate) != PIN_LENGTH or not candidate.isdigit():
                error.configure(text=tr("invalid_pin"))
                return
            result["pin"] = candidate
            window.destroy()

        def cancel() -> None:
            window.destroy()

        confirm = tk.Button(
            buttons,
            text=tr("pair"),
            command=submit,
            activebackground=self._colors.text,
            activeforeground=self._colors.bg,
            disabledforeground=self._colors.dim,
            bg=self._colors.disabled_bg,
            fg=self._colors.dim,
            relief="flat",
            state="disabled",
            padx=self._px(window, 20),
            pady=self._px(window, 6),
        )
        confirm.pack(side="right")

        tk.Button(
            buttons,
            text=tr("cancel"),
            command=cancel,
            activebackground=self._colors.panel,
            activeforeground=self._colors.text,
            bg=self._colors.bg,
            fg=self._colors.muted,
            relief="flat",
            padx=self._px(window, 16),
            pady=self._px(window, 6),
        ).pack(side="right", padx=(0, gap))

        def on_change(*_args: object) -> None:
            candidate = entry_value.get()
            ready = len(candidate) == PIN_LENGTH and candidate.isdigit()
            confirm.configure(
                state="normal" if ready else "disabled",
                bg=self._colors.text if ready else self._colors.disabled_bg,
                fg=self._colors.bg if ready else self._colors.dim,
            )

        trace = entry_value.trace_add("write", on_change)
        window.bind("<Return>", lambda _event: submit())
        window.bind("<Escape>", lambda _event: cancel())

        self._wait(window)
        # Never retain the code beyond the dialog. The trace goes first: its
        # callback configures widgets that the closed window took with it.
        entry_value.trace_remove("write", trace)
        entry_value.set("")
        return result.get("pin", "")

    def _confirm(
        self,
        title: str,
        body: str,
        confirm_text: str,
        *,
        cancel: bool = True,
    ) -> bool:
        window = self._window(title)
        answer: dict[str, bool] = {}
        pad = self._px(window, 24)
        gap = self._px(window, 8)

        title_font = tkfont.Font(family="Microsoft YaHei UI", size=13, weight="bold")
        body_font = tkfont.Font(family="Microsoft YaHei UI", size=10)

        tk.Label(
            window, text=title, bg=self._colors.bg, fg=self._colors.text, font=title_font
        ).pack(padx=pad, pady=(pad, gap), anchor="w")
        tk.Label(
            window,
            text=body,
            bg=self._colors.bg,
            fg=self._colors.muted,
            font=body_font,
            wraplength=self._px(window, 312),
            justify="left",
        ).pack(padx=pad, anchor="w")

        buttons = tk.Frame(window, bg=self._colors.bg)
        buttons.pack(padx=pad, pady=(self._px(window, 16), pad), fill="x")

        def accept() -> None:
            answer["ok"] = True
            window.destroy()

        def reject() -> None:
            answer["ok"] = False
            window.destroy()

        tk.Button(
            buttons,
            text=confirm_text,
            command=accept,
            activebackground=self._colors.text,
            activeforeground=self._colors.bg,
            bg=self._colors.text,
            fg=self._colors.bg,
            relief="flat",
            padx=self._px(window, 20),
            pady=self._px(window, 6),
        ).pack(side="right")

        if cancel:
            tk.Button(
                buttons,
                text=tr("cancel"),
                command=reject,
                activebackground=self._colors.panel,
                activeforeground=self._colors.text,
                bg=self._colors.bg,
                fg=self._colors.muted,
                relief="flat",
                padx=self._px(window, 16),
                pady=self._px(window, 6),
            ).pack(side="right", padx=(0, gap))

        window.bind("<Return>", lambda _event: accept())
        window.bind("<Escape>", lambda _event: reject())

        self._wait(window)
        return answer.get("ok", False)
