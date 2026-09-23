#include "panel_settings.hpp"
#include "nvs.h"

namespace usage_panel::mosaico {
namespace {
constexpr char kNamespace[] = "mosaico";
constexpr char kSettingsKey[] = "settings";
// One NVS integer keeps display and capture preferences in the same storage update.
// Bits 0..6 hold brightness, bit 7 enables capture, and bits 8..31 hold timeout seconds.
constexpr uint32_t kBrightnessMask = 0x7f;
constexpr uint32_t kCameraMask = 0x80;
constexpr unsigned kTimeoutShift = 8;

bool valid(const DisplaySettings& settings)
{
    return settings.brightness >= 1 && settings.brightness <= 100 &&
        settings.clock_timeout_seconds <= 86400;
}
}

PanelSettings load_panel_settings(DisplaySettings defaults)
{
    nvs_handle_t handle;
    if (nvs_open(kNamespace, NVS_READONLY, &handle) != ESP_OK) return {defaults};
    uint32_t packed = 0;
    const esp_err_t error = nvs_get_u32(handle, kSettingsKey, &packed);
    nvs_close(handle);
    const DisplaySettings display{static_cast<uint8_t>(packed & kBrightnessMask),
                                  packed >> kTimeoutShift};
    if (error != ESP_OK || !valid(display)) return {defaults};
    return {display, (packed & kCameraMask) != 0};
}

esp_err_t save_panel_settings(const PanelSettings& settings)
{
    if (!valid(settings.display)) return ESP_ERR_INVALID_ARG;
    const uint32_t packed = settings.display.brightness |
        (settings.camera_enabled ? kCameraMask : 0) |
        (settings.display.clock_timeout_seconds << kTimeoutShift);
    nvs_handle_t handle;
    esp_err_t error = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (error != ESP_OK) return error;
    error = nvs_set_u32(handle, kSettingsKey, packed);
    if (error == ESP_OK) error = nvs_commit(handle);
    nvs_close(handle);
    return error;
}

}  // namespace usage_panel::mosaico
