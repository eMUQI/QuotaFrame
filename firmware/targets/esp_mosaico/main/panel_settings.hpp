#pragma once

#include "usage_panel_state/display_settings.hpp"

namespace usage_panel::mosaico {

struct PanelSettings : DisplaySettings {
    bool gestures = false;
};

PanelSettings load_panel_settings(DisplaySettings defaults);
/** Stores display and local input preferences through a single NVS handle. */
esp_err_t save_panel_settings(const PanelSettings& settings);
bool settings_differ(const PanelSettings& a, const PanelSettings& b);

}  // namespace usage_panel::mosaico
