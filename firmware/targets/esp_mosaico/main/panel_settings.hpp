#pragma once

#include "usage_panel_state/display_settings.hpp"

namespace usage_panel::mosaico {

struct PanelSettings {
    DisplaySettings display;
    bool camera_enabled = false;
};

/** Loads one settings record; missing or invalid records retain defaults and disable capture. */
PanelSettings load_panel_settings(DisplaySettings defaults);
/**
 * Saves all fields in one NVS value. Brightness must be 1..100 and timeout 0..86400 seconds.
 * Returns ESP_ERR_INVALID_ARG for invalid display values, otherwise the NVS operation status.
 */
esp_err_t save_panel_settings(const PanelSettings& settings);

}  // namespace usage_panel::mosaico
