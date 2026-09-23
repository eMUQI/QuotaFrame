"""Cross-platform fault injection for the desktop worker generation boundary."""

import ast
import asyncio
from pathlib import Path
from types import SimpleNamespace
import unittest
from unittest.mock import Mock

from quotaframe_bridge.ui.worker_cleanup import cleanup_worker_tasks


class WorkerCleanupTests(unittest.TestCase):
    def test_restart_waits_for_service_and_ui_task_cleanup(self):
        root = Path(__file__).resolve().parents[1] / "src/quotaframe_bridge/ui"
        for path in (root / "app.py", root / "macos/app.py"):
            with self.subTest(platform=path.parent.name):
                tree = ast.parse(path.read_text())
                methods = [node for node in ast.walk(tree)
                           if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef))
                           and node.name in {"_worker", "_run_background_tasks"}]
                namespace = dict(asyncio=asyncio, cleanup_worker_tasks=cleanup_worker_tasks,
                                 LOGGER=Mock(), time=SimpleNamespace(sleep=lambda _: None),
                                 RESTART_DELAY_S=0)
                exec("from __future__ import annotations\n" + "\n".join(
                    ast.unparse(method) for method in methods), namespace)
                events = []
                owner = SimpleNamespace(_stopping=False, _prompt=Mock())

                async def operation(name):
                    try:
                        await asyncio.Event().wait()
                    finally:
                        await asyncio.sleep(0)
                        events.append(name)

                async def publish(graph):
                    if graph == 1:
                        asyncio.create_task(operation("adoption_closed"))
                        await asyncio.sleep(0)
                        raise RuntimeError("injected status failure")
                    owner._stopping = True

                count = 0

                def build():
                    nonlocal count
                    count += 1
                    if count == 2:
                        self.assertCountEqual(events, ["service_closed", "adoption_closed"])
                    owner._graph = count
                    return SimpleNamespace(run=(lambda: operation("service_closed"))
                                           if count == 1 else idle)

                async def idle():
                    pass

                owner._build_service = build
                owner._publish_status = publish
                owner._monitor_updates = idle
                owner._monitor_firmware_updates = idle
                owner._run_background_tasks = lambda *args: namespace["_run_background_tasks"](owner, *args)
                if path.parent.name == "macos":
                    from quotaframe_bridge.ui.macos.app import MenuBarApplication
                    owner._run_background_tasks = lambda *args: MenuBarApplication._run_background_tasks(owner, *args)
                    from unittest.mock import patch
                    with patch("quotaframe_bridge.ui.macos.app.time.sleep"), patch(
                        "quotaframe_bridge.ui.macos.app.LOGGER", Mock()
                    ):
                        MenuBarApplication._worker(owner)
                else:
                    namespace["_worker"](owner)
                self.assertEqual(count, 2)
                self.assertIsNone(owner._loop)
                self.assertIsNone(owner._graph)

    def test_cleanup_failure_prevents_safe_restart(self):
        loop = asyncio.new_event_loop()
        async def failing_cleanup():
            try:
                await asyncio.Event().wait()
            finally:
                raise RuntimeError("cleanup failed")
        try:
            loop.create_task(failing_cleanup())
            loop.run_until_complete(asyncio.sleep(0))
            with self.assertLogs("quotaframe_bridge.ui.worker_cleanup", level="ERROR"):
                self.assertFalse(cleanup_worker_tasks(loop))
        finally:
            loop.close()
