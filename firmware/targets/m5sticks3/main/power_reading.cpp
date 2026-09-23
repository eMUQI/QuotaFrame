#include "power_reading.hpp"

namespace usage_panel::m5 {

OtaPowerReading derive_power_reading(const BatteryView& view)
{
    OtaPowerReading reading{};
    reading.valid = view.available;
    reading.external_power = view.status != BatteryStatus::Battery;
    reading.percent = view.percent;
    return reading;
}

}  // namespace usage_panel::m5
