"""Exercise Tk request ownership without requiring a platform window server."""

import ast
import asyncio
import concurrent.futures
import logging
from pathlib import Path
import queue
import sys
import threading
from types import SimpleNamespace
import unittest
from unittest.mock import Mock

if sys.platform == "win32":
    from quotaframe_bridge.ui.tk_pin_prompt import TkPinPrompt
else:
    # Execute the production dispatcher while replacing only its window surface.
    path = Path(__file__).resolve().parents[1] / "src/quotaframe_bridge/ui/tk_pin_prompt.py"
    tree = ast.parse(path.read_text())
    cls = next(node for node in tree.body if isinstance(node, ast.ClassDef) and node.name == "TkPinPrompt")
    namespace = dict(asyncio=asyncio, concurrent=concurrent, queue=queue,
                     LOGGER=logging.getLogger(__name__), tk=SimpleNamespace(TclError=RuntimeError),
                     tr=lambda key: key)
    exec("from __future__ import annotations\n" + ast.unparse(cls), namespace)
    TkPinPrompt = namespace["TkPinPrompt"]


class PromptCancellationTests(unittest.IsolatedAsyncioTestCase):
    async def test_cancel_open_confirmation_closes_its_window(self):
        prompt = TkPinPrompt(Mock())
        opened = threading.Event()
        window = Mock()

        def modal(*args):
            prompt._open_dialog = window
            opened.set()
            # The modal Tk loop continues to process queued non-dialog work.
            prompt._pending.get(timeout=2)()
            return False

        prompt._confirm = modal
        task = asyncio.create_task(prompt.confirm_firmware("Panel"))
        await asyncio.sleep(0)
        dialog = asyncio.create_task(asyncio.to_thread(prompt._dialogs.get()))
        self.assertTrue(await asyncio.to_thread(opened.wait, 2))
        task.cancel()
        with self.assertRaises(asyncio.CancelledError):
            await task
        await asyncio.wait_for(dialog, 2)
        window.destroy.assert_called_once_with()
        self.assertIsNone(prompt._dialog_request)

    async def test_delayed_cancel_does_not_close_next_dialog(self):
        prompt = TkPinPrompt(Mock())
        opened = threading.Event()
        dismissed = threading.Event()
        first = Mock()
        second = Mock()

        def modal(*args):
            prompt._open_dialog = first
            opened.set()
            dismissed.wait(2)
            return False

        prompt._confirm = modal
        task = asyncio.create_task(prompt.confirm_firmware("Panel"))
        await asyncio.sleep(0)
        dialog = asyncio.create_task(asyncio.to_thread(prompt._dialogs.get()))
        self.assertTrue(await asyncio.to_thread(opened.wait, 2))
        task.cancel()
        with self.assertRaises(asyncio.CancelledError):
            await task
        dismissed.set()
        await asyncio.wait_for(dialog, 2)

        def next_dialog():
            prompt._open_dialog = second
            prompt._pending.get_nowait()()
            return "123456"

        result = concurrent.futures.Future()
        prompt._resolve(result, next_dialog, ())
        self.assertEqual(result.result(), "123456")
        second.destroy.assert_not_called()

    async def test_cancel_queued_confirmation_never_opens_it(self):
        prompt = TkPinPrompt(Mock())
        other = Mock()
        prompt._open_dialog = other
        prompt._dialog_request = concurrent.futures.Future()
        prompt._confirm = Mock()
        task = asyncio.create_task(prompt.confirm_firmware("Panel"))
        await asyncio.sleep(0)
        task.cancel()
        with self.assertRaises(asyncio.CancelledError):
            await task
        prompt._pending.get_nowait()()
        prompt._dialogs.get_nowait()()
        prompt._confirm.assert_not_called()
        other.destroy.assert_not_called()
