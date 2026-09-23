#include "usage_panel_state/display_settings.hpp"
#include "nvs.h"

namespace usage_panel {

DisplaySettings load_display_settings(DisplaySettings defaults)
{
    nvs_handle_t handle;
    if (nvs_open("display", NVS_READONLY, &handle) != ESP_OK) return defaults;
    uint8_t brightness = 0;
    uint32_t timeout = 0;
    if (nvs_get_u8(handle, "brightness", &brightness) == ESP_OK &&
        brightness >= 1 && brightness <= 100) defaults.brightness = brightness;
    if (nvs_get_u32(handle, "clock_timeout", &timeout) == ESP_OK && timeout <= 86400)
        defaults.clock_timeout_seconds = timeout;
    nvs_close(handle);
    return defaults;
}

esp_err_t save_display_settings(const DisplaySettings& settings)
{
    if (settings.brightness < 1 || settings.brightness > 100 ||
        settings.clock_timeout_seconds > 86400) return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle;
    esp_err_t error = nvs_open("display", NVS_READWRITE, &handle);
    if (error != ESP_OK) return error;
    error = nvs_set_u8(handle, "brightness", settings.brightness);
    if (error == ESP_OK)
        error = nvs_set_u32(handle, "clock_timeout", settings.clock_timeout_seconds);
    if (error == ESP_OK) error = nvs_commit(handle);
    nvs_close(handle);
    return error;
}

}  // namespace usage_panel
