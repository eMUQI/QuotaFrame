#pragma once

#include "driver/i2c_master.h"
#include "usage_panel_state/panel_state.hpp"

namespace usage_panel::amoled {

/**
 * AXP2101 telemetry adapter for the BSP-owned I2C bus.
 *
 * XPowersLib exposes process-global callback hooks, so only one PowerMonitor
 * instance may own the adapter at a time. begin() is idempotent for that owner
 * and rejects a second instance rather than redirecting the global callbacks.
 */
class PowerMonitor {
public:
    PowerMonitor() = default;
    PowerMonitor(const PowerMonitor&) = delete;
    PowerMonitor& operator=(const PowerMonitor&) = delete;

    /** Binds the AXP2101 on an existing I2C master bus. */
    bool begin(i2c_master_bus_handle_t bus);

    /** Reads one raw sample; filtering/debouncing belongs to PowerStateFilter. */
    bool read(RawPowerSample& out);

    /**
     * Consumes one short press of the PWR key.
     *
     * The key reaches the AXP2101's PWRON pin rather than a SoC GPIO, so the
     * press is latched in the PMU interrupt status and polled over I2C. The
     * latch means a press is never missed between polls, only delayed by one
     * poll period. Long presses are left to the PMU's own power-off handling.
     */
    bool take_power_key_short_press();

private:
    bool begun_ = false;
};

}  // namespace usage_panel::amoled
