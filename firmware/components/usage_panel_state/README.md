# usage_panel_state

Hardware-independent behaviour shared by the 480x480 AMOLED targets: page cycling and OTA touch
routing, the screensaver state machine, render scheduling, battery banding,
the screensaver usage view, the usage bar animation and the persisted display
settings.

These modules describe what the panel does, not how a board draws it. A target
owns its display, touch, input routing and power source, and reads no hardware
here: `PowerStateFilter` filters whatever samples the target's power source
produces, and `format_clock_view` formats whatever calendar the target obtained.

Deliberately not part of this component:

- `display_ui.*`, which owns per-target rendering;
- `orientation.*`, whose gravity-to-rotation mapping encodes how one board
  mounts its accelerometer relative to its panel, and is calibration rather
  than shared logic.

See [`docs/PORTING.md`](../../../docs/PORTING.md) for the boundary this follows.
