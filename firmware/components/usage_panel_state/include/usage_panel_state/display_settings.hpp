#pragma once

#include <cstdint>
#include "esp_err.h"

namespace usage_panel {

struct DisplaySettings {
    uint8_t brightness;
    uint32_t clock_timeout_seconds;  // Zero disables automatic clock entry.
};

enum class SettingsAction : uint8_t { None, Open, Dimmer, Brighter, TimeoutOff, Timeout1, Timeout5, Timeout10, Timeout30, Save, Cancel };

/** Loads validated preferences, retaining defaults for missing or invalid fields. */
DisplaySettings load_display_settings(DisplaySettings defaults);
esp_err_t save_display_settings(const DisplaySettings& settings);

inline constexpr uint32_t CLOCK_TIMEOUT_CHOICES[] = {0, 60, 300, 600, 1800};

inline bool select_clock_timeout(SettingsAction action, uint32_t& seconds)
{
    if (action < SettingsAction::TimeoutOff || action > SettingsAction::Timeout30) return false;
    seconds = CLOCK_TIMEOUT_CHOICES[static_cast<uint8_t>(action) -
                                   static_cast<uint8_t>(SettingsAction::TimeoutOff)];
    return true;
}

}  // namespace usage_panel
