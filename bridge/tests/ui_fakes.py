"""Recording doubles for the tray and dialog seams."""

from __future__ import annotations

from quotaframe_bridge.ui.status import TrayState


class FakeTrayShell:
    """Record every call so tests can assert on what the user would see."""

    def __init__(self) -> None:
        self.states: list[TrayState] = []
        self.tooltips: list[str] = []
        self.notifications: list[tuple[str, str]] = []
        self.info_lines: list[tuple[str, ...]] = []
        self.autostart_checked: bool | None = None
        self.firmware_actions: list[tuple[str, bool, str]] = []
        self.bridge_update_actions: list[tuple[bool, str]] = []
        self.stopped = False

    def set_state(self, state: TrayState) -> None:
        self.states.append(state)

    def set_tooltip(self, text: str) -> None:
        self.tooltips.append(text)

    def notify(self, title: str, body: str) -> None:
        self.notifications.append((title, body))

    def set_autostart_checked(self, checked: bool) -> None:
        self.autostart_checked = checked

    def set_info_lines(self, lines: tuple[str, ...]) -> None:
        self.info_lines.append(lines)

    def set_firmware_action(
        self, label: str, enabled: bool, detail: str
    ) -> None:
        self.firmware_actions.append((label, enabled, detail))

    def set_bridge_update_action(self, enabled: bool, detail: str) -> None:
        self.bridge_update_actions.append((enabled, detail))

    def stop(self) -> None:
        self.stopped = True


class FakePinPrompt:
    """Answer pairing prompts with scripted values."""

    def __init__(self, pin: str = "123456", confirm: bool = True) -> None:
        self.pin = pin
        self.confirm = confirm
        self.asked: list[str] = []
        self.confirmed: list[str] = []
        self.forgotten: list[str] = []

    async def ask_pin(self, device_name: str) -> str:
        self.asked.append(device_name)
        return self.pin

    async def confirm_repair(self, device_name: str) -> bool:
        self.confirmed.append(device_name)
        return self.confirm

    async def confirm_forget(self, device_name: str) -> bool:
        self.forgotten.append(device_name)
        return self.confirm
