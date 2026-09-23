"""macOS menu bar leaves of the desktop UI.

The platform-neutral half of the UI — `ui/status.py`, `ui/controller.py`,
`ui/notifications.py` and the `TrayShell` / `PinPromptUI` protocols in
`ui/shell.py` — is shared with Windows unchanged. Only the leaves that touch
AppKit live here.
"""
