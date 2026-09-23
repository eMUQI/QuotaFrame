"""Menu bar artwork.

Two files cover three states, which is the whole point of the macOS design:

- normal  — the panel is connected and collection is working;
- attn    — a device dropped, or collection keeps failing;
- idle    — nothing has connected yet, drawn as the *normal* artwork with the
            system's own dimming (`appearsDisabled`), so no third file exists.

Both are template images: black plus alpha, no second colour. macOS inverts
them for the dark menu bar and again for the highlight behind an open menu, so
the status can never be carried by colour — it is carried by ink mass, from
dimmed outline through outline to solid.
"""

from __future__ import annotations

from pathlib import Path

from quotaframe_bridge.paths import tray_assets
from quotaframe_bridge.ui.status import TrayState

ICON_POINTS = 18

NORMAL_FILENAME = "menubar-normal.svg"
ATTENTION_FILENAME = "menubar-attn.svg"

# Which artwork each state draws, and whether the system dims it. IDLE reuses
# the normal artwork; see the module docstring.
STATE_ARTWORK: dict[TrayState, tuple[str, bool]] = {
    TrayState.OK: (NORMAL_FILENAME, False),
    TrayState.WARN: (ATTENTION_FILENAME, False),
    TrayState.IDLE: (NORMAL_FILENAME, True),
}


class IconError(RuntimeError):
    """The menu bar artwork is missing or unreadable."""


def is_dimmed(state: TrayState) -> bool:
    _filename, dimmed = STATE_ARTWORK[state]
    return dimmed


def load_images(
    image_factory,
    *,
    directory: Path | None = None,
    points: int = ICON_POINTS,
) -> dict[str, object]:
    """Load one template NSImage per artwork file, keyed by filename.

    `image_factory` takes a path and returns something NSImage-shaped, so the
    caller owns the AppKit import and tests can pass a fake.
    """

    root = tray_assets() if directory is None else directory
    images: dict[str, object] = {}
    for filename in (NORMAL_FILENAME, ATTENTION_FILENAME):
        path = root / filename
        image = image_factory(path)
        if image is None:
            raise IconError(f"menu bar artwork could not be loaded: {filename}")
        image.setSize_((points, points))
        # Without this macOS renders the raw black glyph, which disappears on a
        # dark menu bar and stays black when the open menu highlights it.
        image.setTemplate_(True)
        images[filename] = image
    return images
