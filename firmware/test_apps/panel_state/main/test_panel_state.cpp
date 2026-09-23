#include <cstring>

#include "usage_panel_state/panel_state.hpp"
#include "unity.h"

using namespace usage_panel;
using namespace usage_panel;

namespace {

RawPowerSample sample(uint8_t percent)
{
    return {
        .valid = true,
        .battery_present = true,
        .vbus_present = false,
        .charging = false,
        .charge_done = false,
        .percent = percent,
    };
}

}  // namespace

TEST_CASE("clock view formats approved ASCII time and date", "[panel_state]")
{
    LocalCalendarTime calendar{
        .year = 2026,
        .month = 8,
        .day = 12,
        .weekday = 3,
        .hour = 14,
        .minute = 32,
        .second = 59,
        .utc_offset_minutes = 480,
    };

    const ClockView view = format_clock_view(&calendar);

    TEST_ASSERT_TRUE(view.available);
    TEST_ASSERT_EQUAL_STRING("14:32", view.time);
    TEST_ASSERT_EQUAL_STRING("WED", view.weekday);
    TEST_ASSERT_EQUAL_STRING("AUG 12", view.month_day);
    const ClockView unavailable = format_clock_view(nullptr);
    TEST_ASSERT_FALSE(unavailable.available);
    TEST_ASSERT_EQUAL_STRING("--:--", unavailable.time);
    TEST_ASSERT_EQUAL_STRING("", unavailable.weekday);
}

TEST_CASE("battery bands rise at boundaries and fall with hysteresis", "[panel_state]")
{
    PowerStateFilter filter;

    TEST_ASSERT_EQUAL(
        int(BatteryBand::One), int(filter.update(sample(24), 0).band));
    TEST_ASSERT_EQUAL(
        int(BatteryBand::Two), int(filter.update(sample(25), 1).band));
    TEST_ASSERT_EQUAL(
        int(BatteryBand::Three), int(filter.update(sample(50), 2).band));
    TEST_ASSERT_EQUAL(
        int(BatteryBand::Four), int(filter.update(sample(75), 3).band));
    TEST_ASSERT_EQUAL(
        int(BatteryBand::Four), int(filter.update(sample(72), 4).band));
    TEST_ASSERT_EQUAL(
        int(BatteryBand::Three), int(filter.update(sample(71), 5).band));
    TEST_ASSERT_EQUAL(
        int(BatteryBand::Three), int(filter.update(sample(47), 6).band));
    TEST_ASSERT_EQUAL(
        int(BatteryBand::Two), int(filter.update(sample(46), 7).band));
    TEST_ASSERT_EQUAL(
        int(BatteryBand::Two), int(filter.update(sample(22), 8).band));
    TEST_ASSERT_EQUAL(
        int(BatteryBand::One), int(filter.update(sample(21), 9).band));
}

TEST_CASE("charging follows USB samples while full requires five seconds", "[panel_state]")
{
    PowerStateFilter filter;
    auto raw = sample(20);
    BatteryView view = filter.update(raw, 0);
    TEST_ASSERT_FALSE(view.charging);

    raw.vbus_present = true;
    raw.charging = true;
    view = filter.update(raw, 1000);
    TEST_ASSERT_TRUE(view.charging);
    TEST_ASSERT_FALSE(view.full);
    TEST_ASSERT_FALSE(view.low_on_battery);

    raw.vbus_present = false;
    raw.charging = false;
    view = filter.update(raw, 2000);
    TEST_ASSERT_FALSE(view.charging);
    TEST_ASSERT_TRUE(view.low_on_battery);

    raw.vbus_present = true;
    raw.charging = true;
    view = filter.update(raw, 3000);
    TEST_ASSERT_TRUE(view.charging);

    raw.charging = false;
    raw.charge_done = true;
    view = filter.update(raw, 4000);
    TEST_ASSERT_FALSE(view.full);
    view = filter.update(raw, 8999);
    TEST_ASSERT_FALSE(view.full);
    view = filter.update(raw, 9000);
    TEST_ASSERT_TRUE(view.full);
    TEST_ASSERT_TRUE(view.charging);
    TEST_ASSERT_EQUAL(int(BatteryBand::Four), int(view.band));
}

TEST_CASE("missing battery is unavailable even when USB is present", "[panel_state]")
{
    PowerStateFilter filter;
    RawPowerSample raw{
        .valid = true,
        .battery_present = false,
        .vbus_present = true,
        .charging = false,
        .charge_done = true,
        .percent = 100,
    };

    const BatteryView view = filter.update(raw, 0);

    TEST_ASSERT_FALSE(view.available);
    TEST_ASSERT_FALSE(view.full);
    TEST_ASSERT_FALSE(view.charging);
    TEST_ASSERT_EQUAL(int(BatteryBand::Unavailable), int(view.band));
}

TEST_CASE("invalid PMIC reads restart full debounce", "[panel_state]")
{
    PowerStateFilter filter;
    auto raw = sample(20);
    filter.update(raw, 0);
    raw.vbus_present = true;
    raw.charge_done = true;
    TEST_ASSERT_FALSE(filter.update(raw, 1000).full);

    RawPowerSample invalid{};
    TEST_ASSERT_FALSE(filter.update(invalid, 4000).full);
    TEST_ASSERT_FALSE(filter.update(raw, 6000).full);
    TEST_ASSERT_FALSE(filter.update(raw, 10999).full);
    TEST_ASSERT_TRUE(filter.update(raw, 11000).full);
}

TEST_CASE("invalid PMIC reads expire the last good state after fifteen seconds", "[panel_state]")
{
    PowerStateFilter filter;
    const BatteryView good = filter.update(sample(60), 1000);
    TEST_ASSERT_TRUE(good.available);

    RawPowerSample invalid{};
    TEST_ASSERT_TRUE(filter.update(invalid, 15999).available);
    TEST_ASSERT_TRUE(filter.update(invalid, 16000).available);
    const BatteryView expired = filter.update(invalid, 16001);
    TEST_ASSERT_FALSE(expired.available);
    TEST_ASSERT_EQUAL(int(BatteryBand::Unavailable), int(expired.band));
}

TEST_CASE("expired full state requires fresh confirmation after recovery", "[panel_state]")
{
    PowerStateFilter filter;
    auto raw = sample(100);
    raw.vbus_present = true;
    raw.charge_done = true;
    TEST_ASSERT_FALSE(filter.update(raw, 0).full);
    TEST_ASSERT_TRUE(filter.update(raw, 5000).full);

    RawPowerSample invalid{};
    TEST_ASSERT_TRUE(filter.update(invalid, 20000).full);
    TEST_ASSERT_FALSE(filter.update(invalid, 20001).available);

    TEST_ASSERT_FALSE(filter.update(raw, 21000).full);
    TEST_ASSERT_FALSE(filter.update(raw, 25999).full);
    TEST_ASSERT_TRUE(filter.update(raw, 26000).full);
}

TEST_CASE("power reading passes filtered percent on battery", "[panel_state]")
{
    const BatteryView view{
        .available = true,
        .band = BatteryBand::Three,
        .charging = false,
        .full = false,
        .low_on_battery = false,
        .percent = 55,
    };
    const RawPowerSample raw = sample(55);

    const OtaPowerReading reading = derive_power_reading(view, raw);

    TEST_ASSERT_TRUE(reading.valid);
    TEST_ASSERT_FALSE(reading.external_power);
    TEST_ASSERT_EQUAL_UINT8(55, reading.percent);
}

TEST_CASE("power reading keeps USB-only boards gated in via vbus", "[panel_state]")
{
    const BatteryView view{};
    RawPowerSample raw{};
    raw.valid = true;
    raw.vbus_present = true;

    const OtaPowerReading reading = derive_power_reading(view, raw);

    TEST_ASSERT_TRUE(reading.valid);
    TEST_ASSERT_TRUE(reading.external_power);
}

TEST_CASE("power reading fails closed when the PMIC never reported", "[panel_state]")
{
    const BatteryView view{};
    const RawPowerSample raw{};

    const OtaPowerReading reading = derive_power_reading(view, raw);

    TEST_ASSERT_FALSE(reading.valid);
    TEST_ASSERT_FALSE(reading.external_power);
}

TEST_CASE("power reading trusts the filter grace window over a dead poll", "[panel_state]")
{
    const BatteryView view{
        .available = true,
        .band = BatteryBand::Two,
        .charging = false,
        .full = false,
        .low_on_battery = false,
        .percent = 30,
    };
    const RawPowerSample raw{};

    const OtaPowerReading reading = derive_power_reading(view, raw);

    TEST_ASSERT_TRUE(reading.valid);
    TEST_ASSERT_FALSE(reading.external_power);
    TEST_ASSERT_EQUAL_UINT8(30, reading.percent);
}
