"""The two seams that isolate pystray and tkinter from everything else."""

from __future__ import annotations

from typing import Protocol

from quotaframe_bridge.ui.status import TrayState


class TrayShell(Protocol):
    """Everything the controller may ask of the tray icon."""

    def set_state(self, state: TrayState) -> None: ...

    def set_tooltip(self, text: str) -> None: ...

    def notify(self, title: str, body: str) -> None: ...

    def set_autostart_checked(self, checked: bool) -> None: ...

    def set_info_lines(self, lines: tuple[str, ...]) -> None: ...

    def set_firmware_action(
        self, label: str, enabled: bool, detail: str
    ) -> None: ...

    def set_bridge_update_action(self, enabled: bool, detail: str) -> None: ...

    def stop(self) -> None: ...


class PinPromptUI(Protocol):
    """Everything the worker may ask of the dialog thread."""

    async def ask_pin(self, device_name: str) -> str: ...

    async def confirm_repair(self, device_name: str) -> bool: ...

    async def confirm_forget(self, device_name: str) -> bool: ...

    def close_dialogs(self) -> None: ...
