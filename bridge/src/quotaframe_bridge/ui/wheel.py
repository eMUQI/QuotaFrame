"""Normalize wheel detents and bound navigation bursts."""

import time
from collections.abc import Callable


class WheelSteps:
    def __init__(self, now: Callable[[], float] = time.monotonic) -> None:
        self._now = now
        self._delta = 0
        self._last_event = float("-inf")
        self._last_step = float("-inf")

    def reset(self) -> None:
        self._delta = 0

    def feed(self, delta: float) -> int:
        now = self._now()
        if now - self._last_event > 0.5 or self._delta * delta < 0:
            self._delta = 0
        self._last_event = now
        self._delta += delta
        if abs(self._delta) < 120:
            return 0
        direction = -1 if self._delta > 0 else 1
        self._delta = 0
        if now - self._last_step < 0.2:
            return 0
        self._last_step = now
        return direction
