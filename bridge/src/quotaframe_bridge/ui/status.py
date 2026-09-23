"""Immutable panel snapshots and the text derived from them."""

from __future__ import annotations

import enum
import unicodedata
from collections.abc import Mapping
from dataclasses import dataclass

from quotaframe_bridge.i18n import tr
from quotaframe_bridge.domain.models import Provider, ProviderUsage, SourceState

TOOLTIP_PREFIX = "QuotaFrame"
TOOLTIP_LIMIT = 127
# A deterministic proxy for native-menu width; actual pixels still vary by DPI.
DEVICE_SUMMARY_WIDTH = 36
SEPARATOR = " · "
# Only adopted boards have sessions and can be reported as missing.
NO_DEVICES_TEXT = tr("no_devices")
NO_DEVICES_SCANNING_TEXT = tr("no_devices_scanning")

_PROVIDER_LABELS = {Provider.CODEX: "Codex", Provider.CLAUDE: "Claude"}


class TrayState(enum.Enum):
    """What the status dot on the tray icon shows."""

    OK = "ok"
    WARN = "warn"
    IDLE = "idle"


@dataclass(frozen=True, slots=True)
class DeviceStatus:
    """One device session as the tray sees it."""

    label: str
    connected: bool
    ever_connected: bool
    incompatible: bool = False
    address: str = ""
    cleanup_failed: bool = False
    metadata_error: bool = False
    session: object = None

    @property
    def connection_text(self) -> str:
        if self.metadata_error:
            return tr("device_metadata_failed")
        if self.cleanup_failed:
            return tr("ble_cleanup_failed")
        if self.incompatible:
            return "需更新 Bridge/固件"
        if self.connected:
            return tr("connected")
        if self.ever_connected:
            return tr("disconnected")
        # The board is adopted but has not responded during this session.
        return tr("not_found")

    @property
    def text(self) -> str:
        return f"{self.label} {self.connection_text}"


@dataclass(frozen=True, slots=True)
class PanelStatus:
    """One immutable view of everything the tray reports."""

    devices: tuple[DeviceStatus, ...]
    providers: Mapping[Provider, ProviderUsage]
    consecutive_failures: int
    resolution_error: str | None
    adoption_running: bool = False
    incompatible_found: bool = False
    devices_unreadable: bool = False

    @property
    def state(self) -> TrayState:
        if self.devices_unreadable or self.incompatible_found or any(device.incompatible or device.cleanup_failed or device.metadata_error for device in self.devices):
            return TrayState.WARN
        # Without a confirmed incompatibility, an undiscovered device alone
        # does not warrant a warning; source resolution errors remain idle.
        if self.resolution_error is not None:
            return TrayState.IDLE
        if not any(device.ever_connected for device in self.devices):
            return TrayState.IDLE
        if any(
            device.ever_connected and not device.connected
            for device in self.devices
        ):
            return TrayState.WARN
        return TrayState.OK


def _provider_text(provider: Provider, usage: ProviderUsage | None) -> str:
    label = _PROVIDER_LABELS[provider]
    if usage is None or usage.state is SourceState.UNAVAILABLE:
        return f"{label} --"
    window = usage.short or usage.week
    if window is None:
        return f"{label} --"
    period = tr("usage_short") if usage.short is not None else tr("usage_week")
    return f"{label} {period} {window.used_percent}% {tr('usage_used')}"


def display_width(text: str) -> int:
    """Return a deterministic terminal-cell proxy for native menu width."""

    return sum(
        0
        if unicodedata.combining(character)
        else 2
        if unicodedata.east_asian_width(character) in {"F", "W"}
        else 1
        for character in text
    )


def format_device_summary(devices: tuple[DeviceStatus, ...]) -> str:
    """Return the bounded connection summary shared by tray platforms."""

    if not devices:
        return NO_DEVICES_TEXT
    if any(device.cleanup_failed for device in devices):
        return tr("ble_cleanup_failed")
    incompatible = tuple(device for device in devices if device.incompatible)
    if incompatible:
        if len(incompatible) == 1 and display_width(incompatible[0].text) <= DEVICE_SUMMARY_WIDTH:
            return incompatible[0].text
        return f"{len(incompatible)} 台需更新 Bridge/固件"
    if len(devices) == 1:
        device = devices[0]
        if display_width(device.text) <= DEVICE_SUMMARY_WIDTH:
            return device.text
        return tr("single_device", separator=SEPARATOR, state=device.connection_text)

    connected = sum(device.connected for device in devices)
    if connected == len(devices):
        return tr("all_connected", count=len(devices), separator=SEPARATOR)

    exceptions = tuple(device for device in devices if not device.connected)
    if len(exceptions) == 1:
        named = tr(
            "named_exception", device=exceptions[0].text,
            separator=SEPARATOR, connected=connected,
        )
        if display_width(named) <= DEVICE_SUMMARY_WIDTH:
            return named

    return tr(
        "device_counts", count=len(devices), separator=SEPARATOR,
        connected=connected, disconnected=len(devices) - connected,
    )


def format_info_lines(status: PanelStatus) -> tuple[str, str]:
    """Return the two disabled information lines shown in the tray menu."""

    devices = (
        "新设备需更新 Bridge/固件"
        if status.incompatible_found
        else (
            NO_DEVICES_SCANNING_TEXT
            if not status.devices and status.adoption_running
            else format_device_summary(status.devices)
        )
    )
    providers = SEPARATOR.join(
        _provider_text(provider, status.providers.get(provider)) for provider in Provider
    )
    if status.devices_unreadable:
        devices = tr("devices_unreadable")
    return (devices, providers)


def format_tooltip(status: PanelStatus) -> str:
    """Return a single-line tooltip within the Windows length limit."""

    devices, providers = format_info_lines(status)
    parts = [part for part in (TOOLTIP_PREFIX, devices, providers) if part]
    tooltip = SEPARATOR.join(parts)
    if len(tooltip) <= TOOLTIP_LIMIT:
        return tooltip
    return tooltip[: TOOLTIP_LIMIT - 1] + "…"
