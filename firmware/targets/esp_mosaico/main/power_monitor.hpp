#pragma once

#include "usage_panel_state/panel_state.hpp"

namespace usage_panel::mosaico {

/**
 * BQ27220 fuel-gauge adapter for the BSP-owned I2C bus.
 *
 * The board has no PMIC, so charger state is derived from the gauge status
 * word rather than read from a charger register.
 */
class PowerMonitor {
public:
    PowerMonitor() = default;
    PowerMonitor(const PowerMonitor&) = delete;
    PowerMonitor& operator=(const PowerMonitor&) = delete;

    /** Attaches the gauge. The display must be started first: it owns the rail and the I2C bus. */
    bool begin();

    /** Reads one raw sample; filtering/debouncing belongs to PowerStateFilter. */
    bool read(RawPowerSample& out);

private:
    bool begun_ = false;
};

}  // namespace usage_panel::mosaico
