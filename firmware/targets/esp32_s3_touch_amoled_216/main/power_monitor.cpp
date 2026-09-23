#include "power_monitor.hpp"

#include <array>
#include <cstring>

#define XPOWERS_CHIP_AXP2101
#include "XPowersLib.h"

namespace usage_panel::amoled {
namespace {

constexpr int kI2cTimeoutMs = 1000;
// XPowersLib accepts plain function pointers rather than a per-instance context.
// Keep one registered device/owner so its callbacks cannot accidentally talk to
// a different I2C handle if another PowerMonitor object is constructed later.
XPowersPMU g_pmu;
i2c_master_dev_handle_t g_device = nullptr;
PowerMonitor* g_owner = nullptr;

int register_read(
    uint8_t, uint8_t address, uint8_t* data, uint8_t length)
{
    if (!g_device || !data || length == 0) return -1;
    return i2c_master_transmit_receive(
               g_device, &address, 1, data, length, kI2cTimeoutMs) == ESP_OK
        ? 0
        : -1;
}

int register_write(
    uint8_t, uint8_t address, uint8_t* data, uint8_t length)
{
    if (!g_device || (!data && length != 0)) return -1;
    // AXP2101 register writes place the register address before the payload.
    // 255 payload bytes plus that address byte fit the fixed scratch buffer.
    std::array<uint8_t, 256> buffer{};
    buffer[0] = address;
    if (length != 0) std::memcpy(buffer.data() + 1, data, length);
    return i2c_master_transmit(
               g_device, buffer.data(), static_cast<size_t>(length) + 1,
               kI2cTimeoutMs) == ESP_OK
        ? 0
        : -1;
}

}  // namespace

bool PowerMonitor::begin(i2c_master_bus_handle_t bus)
{
    if (!bus) return false;
    if (begun_) return g_owner == this;
    // See the callback globals above: a second live owner cannot be represented
    // safely by XPowersLib's callback API, so fail instead of silently stealing it.
    if (g_owner) return false;

    const i2c_device_config_t config{
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = AXP2101_SLAVE_ADDRESS,
        .scl_speed_hz = 400000,
        .scl_wait_us = 0,
        .flags = {},
    };
    if (i2c_master_bus_add_device(bus, &config, &g_device) != ESP_OK) {
        g_device = nullptr;
        return false;
    }
    g_owner = this;
    if (!g_pmu.begin(
            AXP2101_SLAVE_ADDRESS, register_read, register_write)) {
        i2c_master_bus_rm_device(g_device);
        g_device = nullptr;
        g_owner = nullptr;
        return false;
    }
    // isPekeyShortPressIrq() tests the library's enable-register cache before
    // the status bit, so the interrupt has to be enabled here or no press is
    // ever observable. Only the short-press source is enabled; long-press
    // power-off stays with the PMU.
    g_pmu.clearIrqStatus();
    g_pmu.enableIRQ(XPOWERS_AXP2101_PKEY_SHORT_IRQ);
    begun_ = true;
    return true;
}

bool PowerMonitor::take_power_key_short_press()
{
    if (!begun_ || g_owner != this) return false;
    g_pmu.getIrqStatus();
    if (!g_pmu.isPekeyShortPressIrq()) return false;
    g_pmu.clearIrqStatus();
    return true;
}

bool PowerMonitor::read(RawPowerSample& out)
{
    if (!begun_ || g_owner != this) return false;
    const int percent = g_pmu.getBatteryPercent();
    if (percent < 0 || percent > 100) return false;

    out = {
        .valid = true,
        .battery_present = g_pmu.isBatteryConnect(),
        .vbus_present = g_pmu.isVbusIn() && g_pmu.isVbusGood(),
        .charging = g_pmu.isCharging(),
        .charge_done =
            g_pmu.getChargerStatus() == XPOWERS_AXP2101_CHG_DONE_STATE,
        .percent = static_cast<uint8_t>(percent),
    };
    return true;
}

}  // namespace usage_panel::amoled
