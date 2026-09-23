#include "camera_setting.hpp"
#include "nvs.h"

namespace usage_panel::mosaico {
namespace {
constexpr char kNamespace[] = "camera";
constexpr char kEnabledKey[] = "enabled";
}

bool load_camera_enabled()
{
    nvs_handle_t handle;
    if (nvs_open(kNamespace, NVS_READONLY, &handle) != ESP_OK) return false;
    uint8_t enabled = 0;
    const bool stored = nvs_get_u8(handle, kEnabledKey, &enabled) == ESP_OK && enabled <= 1;
    nvs_close(handle);
    return stored && enabled;
}

esp_err_t save_camera_enabled(bool enabled)
{
    nvs_handle_t handle;
    esp_err_t error = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (error != ESP_OK) return error;
    error = nvs_set_u8(handle, kEnabledKey, enabled ? 1 : 0);
    if (error == ESP_OK) error = nvs_commit(handle);
    nvs_close(handle);
    return error;
}

}  // namespace usage_panel::mosaico
