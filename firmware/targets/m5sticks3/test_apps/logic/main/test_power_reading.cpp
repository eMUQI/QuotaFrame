#include "unity.h"

#include "battery_view.hpp"
#include "power_reading.hpp"

using usage_panel::m5::BatteryStateFilter;
using usage_panel::m5::BatteryStatus;
using usage_panel::m5::BatteryView;
using usage_panel::m5::derive_power_reading;

namespace {
constexpr int kNoVbusMv = 3300;
}  // namespace

TEST_CASE("an unavailable view fails closed", "[power_reading]")
{
    const auto reading = derive_power_reading(BatteryView{});
    TEST_ASSERT_FALSE(reading.valid);
    TEST_ASSERT_FALSE(reading.external_power);
    TEST_ASSERT_EQUAL_UINT8(0, reading.percent);
}

TEST_CASE("on-battery verdicts pass the stable view through", "[power_reading]")
{
    BatteryView view{};
    view.available = true;
    view.status = BatteryStatus::Battery;
    view.percent = 50;
    view.band = 3;

    const auto reading = derive_power_reading(view);
    TEST_ASSERT_TRUE(reading.valid);
    TEST_ASSERT_FALSE(reading.external_power);
    TEST_ASSERT_EQUAL_UINT8(50, reading.percent);
}

TEST_CASE("every externally powered status counts as external power",
          "[power_reading]")
{
    const BatteryStatus external[] = {
        BatteryStatus::Usb, BatteryStatus::Charging, BatteryStatus::Full};
    for (const auto status : external) {
        BatteryView view{};
        view.available = true;
        view.status = status;
        view.percent = 87;

        const auto reading = derive_power_reading(view);
        TEST_ASSERT_TRUE(reading.valid);
        TEST_ASSERT_TRUE(reading.external_power);
        TEST_ASSERT_EQUAL_UINT8(87, reading.percent);
    }
}

TEST_CASE("the gate rides the holdover window before failing closed",
          "[power_reading]")
{
    BatteryStateFilter filter;
    for (int i = 0; i < 5; ++i) {
        const auto reading =
            derive_power_reading(filter.update(60, false, kNoVbusMv));
        TEST_ASSERT_TRUE(reading.valid);
    }

    // Transient read failures keep the last good verdict; only after the
    // holdover expires does the gate see an invalid reading.
    for (int i = 0; i < 15; ++i) {
        const auto held =
            derive_power_reading(filter.update(-1, false, kNoVbusMv));
        TEST_ASSERT_TRUE(held.valid);
    }
    const auto expired =
        derive_power_reading(filter.update(-1, false, kNoVbusMv));
    TEST_ASSERT_FALSE(expired.valid);
    TEST_ASSERT_FALSE(expired.external_power);
}
