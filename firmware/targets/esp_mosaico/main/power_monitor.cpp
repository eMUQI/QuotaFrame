#include "power_monitor.hpp"

#include "bsp/esp_mosaico.h"
#include "esp_log.h"

namespace usage_panel::mosaico {
namespace {
constexpr char TAG[] = "mosaico_power";

// BQ27220 battery status word, low bit first.
constexpr uint16_t STATUS_DISCHARGING = 1U << 0;
constexpr uint16_t STATUS_BATTERY_PRESENT = 1U << 3;
constexpr uint16_t STATUS_FULL_CHARGED = 1U << 9;
}  // namespace

bool PowerMonitor::begin()
{
    if (begun_) {
        return true;
    }
    const esp_err_t error = bsp_battery_init();
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "BQ27220 initialization failed: %s", esp_err_to_name(error));
        return false;
    }
    begun_ = true;
    return true;
}

bool PowerMonitor::read(RawPowerSample& out)
{
    out = {};
    if (!begun_) {
        return false;
    }
    bsp_battery_status_t status{};
    const esp_err_t error = bsp_battery_read(&status);
    if (error != ESP_OK) {
        ESP_LOGW(TAG, "BQ27220 read failed: %s", esp_err_to_name(error));
        return false;
    }

    const bool discharging = (status.status_flags & STATUS_DISCHARGING) != 0;
    out.valid = true;
    out.battery_present = (status.status_flags & STATUS_BATTERY_PRESENT) != 0;
    out.charge_done = (status.status_flags & STATUS_FULL_CHARGED) != 0;
    // The gauge sees the battery, not the USB rail. Anything that stops the
    // pack from discharging is external power as far as the UI and the OTA
    // power gate are concerned.
    out.vbus_present = !discharging;
    out.charging = !discharging && !out.charge_done;
    out.percent = status.state_of_charge > 100 ? 100 : status.state_of_charge;
    return true;
}

}  // namespace usage_panel::mosaico
