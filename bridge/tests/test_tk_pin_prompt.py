from __future__ import annotations

import asyncio
import sys
import unittest

if sys.platform != "win32":
    raise unittest.SkipTest("the Tk pairing prompt is part of the Windows tray build")

import tkinter as tk

from quotaframe_bridge.i18n import tr
from quotaframe_bridge.ui.tk_pin_prompt import TkPinPrompt, apply_title_bar_theme


class _IdleRoot:
    """Runs idle work deterministically without starting a Tk event loop."""

    def __init__(self) -> None:
        self.callbacks: list[object] = []

    def after(self, _delay: int, callback: object) -> None:
        self.callbacks.append(callback)

    def run_next(self) -> None:
        callback = self.callbacks.pop(0)
        callback()  # type: ignore[operator]


class _Window:
    def __init__(self, *, required: tuple[int, int] = (295, 273), dpi: float = 96.0) -> None:
        self.geometries: list[str] = []
        self.required = required
        self.dpi = dpi

    def geometry(self, geometry: str) -> None:
        self.geometries.append(geometry)

    def update_idletasks(self) -> None:
        return None

    def winfo_fpixels(self, _distance: str) -> float:
        return self.dpi

    def winfo_reqwidth(self) -> int:
        return self.required[0]

    def winfo_reqheight(self) -> int:
        return self.required[1]

    def winfo_screenwidth(self) -> int:
        return 1920

    def winfo_screenheight(self) -> int:
        return 1080


class TkPinPromptTests(unittest.IsolatedAsyncioTestCase):
    async def test_forget_confirmation_describes_bridge_removal(self) -> None:
        prompt = TkPinPrompt(_IdleRoot())  # type: ignore[arg-type]
        captured: list[tuple[object, ...]] = []

        async def dispatch(_function: object, *args: object) -> bool:
            captured.append(args)
            return False

        prompt._on_main_thread = dispatch  # type: ignore[method-assign]

        self.assertFalse(await prompt.confirm_forget("M5"))
        self.assertEqual(
            captured,
            [(tr("forget_device", device_name='M5'), tr("forget_body"), tr("forget"))],
        )

    async def test_call_soon_runs_a_non_dialog_callback_on_the_root_thread(self) -> None:
        """Fails if tray commands bypass the Tk-owned dispatch queue."""

        root = _IdleRoot()
        prompt = TkPinPrompt(root)  # type: ignore[arg-type]
        observed: list[str] = []

        prompt.call_soon(observed.append, "quit")
        root.run_next()

        self.assertEqual(observed, ["quit"])

    async def test_dispatcher_survives_a_non_dialog_callback_failure(self) -> None:
        """One tray update failure must not disable later dialogs or Exit."""

        root = _IdleRoot()
        prompt = TkPinPrompt(root)  # type: ignore[arg-type]
        observed: list[str] = []

        def fails() -> None:
            raise RuntimeError("simulated tray update failure")

        prompt.call_soon(fails)
        prompt.call_soon(observed.append, "still running")
        root.run_next()

        self.assertEqual(observed, ["still running"])
        self.assertEqual(len(root.callbacks), 1)

    async def test_ask_pin_marshals_the_dialog_result_through_the_root(self) -> None:
        """Fails if async callers no longer receive the main-thread dialog answer."""

        root = _IdleRoot()
        prompt = TkPinPrompt(root)  # type: ignore[arg-type]
        prompt._ask_pin = lambda device_name: "654321"  # type: ignore[method-assign]

        task = asyncio.create_task(prompt.ask_pin("M5StickS3"))
        await asyncio.sleep(0)
        root.run_next()
        result = await task

        self.assertEqual(result, "654321")
        self.assertEqual(len(root.callbacks), 1)

    async def test_pump_rearms_itself_before_running_a_callback(self) -> None:
        """A dialog's nested Tk loop only pumps timers that are already armed."""

        root = _IdleRoot()
        prompt = TkPinPrompt(root)  # type: ignore[arg-type]
        armed: list[int] = []

        prompt.call_soon(lambda: armed.append(len(root.callbacks)))
        root.run_next()

        self.assertEqual(armed, [1])

    async def test_exit_is_dispatched_while_a_dialog_is_open(self) -> None:
        """Fails if a modal dialog can strand the Exit command in the queue."""

        root = _IdleRoot()
        prompt = TkPinPrompt(root)  # type: ignore[arg-type]
        observed: list[str] = []

        def blocking_confirm(*_args: object, **_kwargs: object) -> bool:
            # Stand in for wait_window: the tray thread asks to quit while the
            # dialog is up, and Tk keeps servicing armed timers meanwhile.
            prompt.call_soon(observed.append, "quit")
            root.run_next()
            return True

        prompt._confirm = blocking_confirm  # type: ignore[method-assign]
        task = asyncio.create_task(prompt.confirm_repair("M5"))
        await asyncio.sleep(0)
        root.run_next()

        self.assertTrue(await task)
        self.assertEqual(observed, ["quit"])

    async def test_only_one_dialog_runs_at_a_time(self) -> None:
        """Two tray clicks must not stack two modal windows on each other."""

        root = _IdleRoot()
        prompt = TkPinPrompt(root)  # type: ignore[arg-type]
        depth = 0
        peak = 0

        def nested_confirm(*_args: object, **_kwargs: object) -> bool:
            nonlocal depth, peak
            depth += 1
            peak = max(peak, depth)
            root.run_next()
            depth -= 1
            return True

        prompt._confirm = nested_confirm  # type: ignore[method-assign]
        first = asyncio.create_task(prompt.confirm_repair("M5"))
        second = asyncio.create_task(prompt.confirm_repair("WS"))
        await asyncio.sleep(0)
        root.run_next()
        root.run_next()

        self.assertTrue(await first)
        self.assertTrue(await second)
        self.assertEqual(peak, 1)

    async def test_center_uses_the_deck_size_when_the_content_fits(self) -> None:
        """The 360x240 deck layout stays the floor at the design resolution."""

        prompt = TkPinPrompt(_IdleRoot())  # type: ignore[arg-type]
        window = _Window(required=(200, 200))

        prompt._center(window)  # type: ignore[arg-type]

        self.assertEqual(window.geometries, ["360x240+780+420"])

    async def test_center_grows_rather_than_clipping_the_buttons(self) -> None:
        """The dialog grows to fit its 273 px of content instead of clipping."""

        prompt = TkPinPrompt(_IdleRoot())  # type: ignore[arg-type]
        window = _Window(required=(295, 273))

        prompt._center(window)  # type: ignore[arg-type]

        self.assertEqual(window.geometries, ["360x273+780+403"])

    async def test_center_scales_the_deck_floor_with_the_display(self) -> None:
        """Pixel geometry alone shrinks the dialog on a high-DPI display."""

        prompt = TkPinPrompt(_IdleRoot())  # type: ignore[arg-type]
        window = _Window(required=(200, 200), dpi=120.0)

        prompt._center(window)  # type: ignore[arg-type]

        self.assertEqual(window.geometries, ["450x300+735+390"])


class TkDialogWindowTests(unittest.TestCase):
    """Exercise real Tk because the withdrawn root owns the dialogs."""

    def setUp(self) -> None:
        try:
            self.root = tk.Tk()
        except tk.TclError:  # pragma: no cover - headless build agent
            self.skipTest("no display available")
        self.root.withdraw()
        self.prompt = TkPinPrompt(self.root)

    def tearDown(self) -> None:
        self.root.destroy()

    def test_a_dialog_owned_by_the_withdrawn_root_is_actually_displayed(self) -> None:
        """Tk hands a transient window its master's state, and ours is hidden."""

        window = self.prompt._window(tr("pair_panel"))
        try:
            window.update()
            self.assertEqual(window.state(), "normal")
            self.assertTrue(window.winfo_ismapped())
        finally:
            window.grab_release()
            window.destroy()

    def test_close_dialogs_destroys_the_open_window(self) -> None:
        """Exit has to end the nested wait_window loop that root.quit cannot."""

        window = self.prompt._window(tr("pair_panel"))
        window.update()

        self.prompt.close_dialogs()

        self.assertFalse(window.winfo_exists())

    def test_close_dialogs_is_harmless_when_no_dialog_is_open(self) -> None:
        self.prompt.close_dialogs()

    def test_the_dialog_accepts_both_title_bar_themes(self) -> None:
        """Both application modes must be accepted by the native frame API."""

        window = tk.Toplevel(self.root)
        window.update_idletasks()
        try:
            for dark in (False, True):
                with self.subTest(dark=dark):
                    self.assertTrue(apply_title_bar_theme(window, dark=dark))
        finally:
            window.destroy()

    def test_the_pin_dialog_never_clips_its_own_buttons(self) -> None:
        """The pair and cancel buttons are the last rows, so they must stay visible."""

        shown: list[tuple[int, int, int, int]] = []

        def capture(window: tk.Toplevel) -> None:
            self.prompt._center(window)
            window.update_idletasks()
            shown.append(
                (
                    window.winfo_width(),
                    window.winfo_height(),
                    window.winfo_reqwidth(),
                    window.winfo_reqheight(),
                )
            )
            window.destroy()

        self.prompt._wait = capture  # type: ignore[method-assign]
        self.prompt._ask_pin("WS-Usage-3512")

        width, height, required_width, required_height = shown[0]
        self.assertGreaterEqual(width, required_width)
        self.assertGreaterEqual(height, required_height)


if __name__ == "__main__":
    unittest.main()
