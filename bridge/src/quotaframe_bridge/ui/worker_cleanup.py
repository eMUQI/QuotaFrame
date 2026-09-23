"""Retire all tasks on the desktop worker's dedicated event loop."""

import asyncio
import logging

LOGGER = logging.getLogger(__name__)


async def _drain_tasks() -> bool:
    # Flush UI submissions queued before the application disabled its loop handle.
    await asyncio.sleep(0)
    deadline = asyncio.get_running_loop().time() + 30.0
    clean = True
    while pending := asyncio.all_tasks() - {asyncio.current_task()}:
        for task in pending:
            if not task.cancelling():
                task.cancel()
        finished, unfinished = await asyncio.wait(
            pending, timeout=max(0.0, deadline - asyncio.get_running_loop().time())
        )
        for task in finished:
            if not task.cancelled() and (error := task.exception()) is not None:
                LOGGER.error("Background task cleanup failed", exc_info=error)
                clean = False
        if unfinished:
            LOGGER.error("Background cleanup timed out; automatic restart disabled")
            return False
    return clean


def cleanup_worker_tasks(loop: asyncio.AbstractEventLoop) -> bool:
    """Await all tasks on the worker-owned loop; return whether restart is safe.

    Callers disable UI submissions before cleanup. This includes adoption and
    firmware tasks scheduled outside the four top-level background coroutines.
    """
    return loop.run_until_complete(_drain_tasks())
