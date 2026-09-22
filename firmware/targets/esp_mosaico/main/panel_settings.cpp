#include "panel_settings.hpp"
#include "nvs.h"

namespace usage_panel::mosaico {

PanelSettings load_panel_settings(DisplaySettings defaults)
{
    PanelSettings result{load_display_settings(defaults), false};
    nvs_handle_t handle;
    if (nvs_open("display", NVS_READONLY, &handle) == ESP_OK) {
        uint8_t enabled = 0;
        if (nvs_get_u8(handle, "gestures", &enabled) == ESP_OK && enabled <= 1)
            result.gestures = enabled != 0;
        nvs_close(handle);
    }
    return result;
}

esp_err_t save_panel_settings(const PanelSettings& settings)
{
    if (settings.brightness < 1 || settings.brightness > 100 || settings.clock_timeout_seconds > 86400)
        return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle;
    esp_err_t error = nvs_open("display", NVS_READWRITE, &handle);
    if (error != ESP_OK) return error;
    error = nvs_set_u8(handle, "brightness", settings.brightness);
    if (error == ESP_OK) error = nvs_set_u32(handle, "clock_timeout", settings.clock_timeout_seconds);
    if (error == ESP_OK) error = nvs_set_u8(handle, "gestures", settings.gestures);
    if (error == ESP_OK) error = nvs_commit(handle);
    nvs_close(handle);
    return error;
}

bool settings_differ(const PanelSettings& a, const PanelSettings& b)
{
    return a.brightness != b.brightness || a.clock_timeout_seconds != b.clock_timeout_seconds ||
        a.gestures != b.gestures;
}

}  // namespace usage_panel::mosaico
