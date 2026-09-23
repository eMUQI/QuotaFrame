"""Windows application-mode preference and colors for custom dialogs."""

from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class DialogColors:
    bg: str
    panel: str
    text: str
    muted: str
    dim: str
    error: str
    disabled_bg: str


DARK = DialogColors(
    bg="#0A0C10", panel="#171B22", text="#F2F4F7", muted="#8B93A1",
    dim="#565D68", error="#F26D6D", disabled_bg="#1D2128",
)
LIGHT = DialogColors(
    bg="#F7F8FA", panel="#FFFFFF", text="#171B22", muted="#535D6B",
    dim="#727C89", error="#B42318", disabled_bg="#E4E7EC",
)


def apps_use_dark_theme() -> bool:
    """Read application mode, independently of the taskbar's Windows mode."""

    try:
        import winreg

        with winreg.OpenKey(
            winreg.HKEY_CURRENT_USER,
            r"Software\Microsoft\Windows\CurrentVersion\Themes\Personalize",
        ) as key:
            value, kind = winreg.QueryValueEx(key, "AppsUseLightTheme")
        return kind == winreg.REG_DWORD and value == 0
    except (ImportError, OSError):
        return False
