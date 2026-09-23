#pragma once

#include "battery_view.hpp"
#include "usage_ota/power_gate.hpp"

namespace usage_panel::m5 {

/** Derives the OTA power-gate verdict from the battery filter's stable view.
 *  External power uses the same VBUS-authoritative signal as the corner icon
 *  (`status != Battery`), so an unplugged cable or an expired telemetry
 *  holdover fails closed instead of trusting the bouncing charge pin. */
OtaPowerReading derive_power_reading(const BatteryView& view);

}  // namespace usage_panel::m5
