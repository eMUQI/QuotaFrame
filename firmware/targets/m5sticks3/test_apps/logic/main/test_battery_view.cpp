#include "unity.h"

#include "battery_view.hpp"

using usage_panel::m5::BatteryStateFilter;
using usage_panel::m5::BatteryStatus;
using usage_panel::m5::BatteryView;

namespace {
constexpr int kVbusMv = 4500;
constexpr int kNoVbusMv = 3300;

BatteryView settled(int level, bool charging, int samples,
                    int vbus_mv = kNoVbusMv)
{
    BatteryStateFilter filter;
    BatteryView view{};
    for (int i = 0; i < samples; ++i) {
        view = filter.update(level, charging, vbus_mv);
    }
    return view;
}
}  // namespace

TEST_CASE("usb presence requires vbus above 4000 mv", "[battery_view]")
{
    const auto unplugged = settled(50, false, 3);
    TEST_ASSERT_EQUAL(BatteryStatus::Battery, unplugged.status);

    const auto vbus_powered = settled(50, false, 3, kVbusMv);
    TEST_ASSERT_EQUAL(BatteryStatus::Usb, vbus_powered.status);
}

TEST_CASE("an unreadable vbus reading falls back to the charge pin", "[battery_view]")
{
    BatteryStateFilter filter;

    filter.update(-1, true, 0);
    const auto engaged = filter.update(-1, true, 0);
    TEST_ASSERT_EQUAL(BatteryStatus::Charging, engaged.status);
}

TEST_CASE("fallback charge verdict rides out single bounced releases", "[battery_view]")
{
    BatteryStateFilter filter;
    BatteryView view{};
    for (int i = 0; i < 4; ++i) view = filter.update(50, true, 0);
    TEST_ASSERT_EQUAL(BatteryStatus::Charging, view.status);

    // Without VBUS the debounced verdict is the only signal; one bounced release
    // must not flip the icon to battery power.
    view = filter.update(50, false, 0);
    TEST_ASSERT_EQUAL(BatteryStatus::Charging, view.status);

    for (int i = 0; i < 6; ++i) view = filter.update(50, false, 0);
    TEST_ASSERT_EQUAL(BatteryStatus::Battery, view.status);
}

TEST_CASE("unplug is detected on the first sample with low vbus", "[battery_view]")
{
    BatteryStateFilter filter;
    filter.update(50, true, kVbusMv);

    // Even with the status pin still asserting, losing VBUS means unplugged.
    const auto gone = filter.update(50, true, 3300);
    TEST_ASSERT_EQUAL(BatteryStatus::Battery, gone.status);
}

TEST_CASE("charge indicator needs two consecutive active samples", "[battery_view]")
{
    BatteryStateFilter filter;

    filter.update(50, true, kVbusMv);
    const auto engaged = filter.update(50, true, kVbusMv);
    TEST_ASSERT_EQUAL(BatteryStatus::Charging, engaged.status);
}

TEST_CASE("alternating charge pin bounces never light the indicator", "[battery_view]")
{
    BatteryStateFilter filter;

    BatteryView view{};
    for (int i = 0; i < 6; ++i) {
        view = filter.update(50, i % 2 == 0, kVbusMv);
        TEST_ASSERT_NOT_EQUAL(BatteryStatus::Charging, view.status);
    }
}

TEST_CASE("charge indicator holds through brief pin releases", "[battery_view]")
{
    BatteryStateFilter filter;
    BatteryView view{};
    for (int i = 0; i < 2; ++i) view = filter.update(50, true, kVbusMv);
    TEST_ASSERT_EQUAL(BatteryStatus::Charging, view.status);

    // Observed bounce bursts stay short; the bolt must ride them out.
    for (int i = 0; i < 5; ++i) {
        view = filter.update(50, false, kVbusMv);
        TEST_ASSERT_EQUAL(BatteryStatus::Charging, view.status);
    }
    view = filter.update(50, false, kVbusMv);
    TEST_ASSERT_EQUAL(BatteryStatus::Usb, view.status);
}

TEST_CASE("unplugging returns to battery state immediately", "[battery_view]")
{
    BatteryStateFilter filter;
    BatteryView view{};
    for (int i = 0; i < 5; ++i) view = filter.update(80, true, kVbusMv);
    TEST_ASSERT_EQUAL(BatteryStatus::Charging, view.status);

    view = filter.update(80, false, kNoVbusMv);
    TEST_ASSERT_EQUAL(BatteryStatus::Battery, view.status);
}

TEST_CASE("unreadable levels hide the indicator only off external power", "[battery_view]")
{
    const auto unknown = settled(-1, false, 3);
    TEST_ASSERT_FALSE(unknown.available);

    const auto unknown_on_usb = settled(-1, false, 3, kVbusMv);
    TEST_ASSERT_TRUE(unknown_on_usb.available);
    TEST_ASSERT_EQUAL_UINT8(0, unknown_on_usb.percent);
}

TEST_CASE("transient read failures keep the last good reading", "[battery_view]")
{
    BatteryStateFilter filter;
    for (int i = 0; i < 5; ++i) filter.update(60, false, kNoVbusMv);

    const auto held = filter.update(-1, false, kNoVbusMv);
    TEST_ASSERT_TRUE(held.available);
    TEST_ASSERT_EQUAL_UINT8(60, held.percent);
}

TEST_CASE("persistent read failures expire the cached level", "[battery_view]")
{
    BatteryStateFilter filter;
    for (int i = 0; i < 5; ++i) filter.update(60, false, kNoVbusMv);

    // Holdover window (15 samples) plus one; then the stale reading must be
    // dropped instead of presenting a trustworthy-looking battery forever.
    for (int i = 0; i < 15; ++i) {
        const auto view = filter.update(-1, false, kNoVbusMv);
        TEST_ASSERT_TRUE(view.available);
    }
    const auto expired = filter.update(-1, false, kNoVbusMv);
    TEST_ASSERT_FALSE(expired.available);

    // A fresh valid reading rebuilds state from scratch.
    filter.update(40, false, kNoVbusMv);
    const auto rebuilt = filter.update(40, false, kNoVbusMv);
    TEST_ASSERT_TRUE(rebuilt.available);
    TEST_ASSERT_EQUAL_UINT8(40, rebuilt.percent);
}

TEST_CASE("failed reads cannot complete the full countdown", "[battery_view]")
{
    BatteryStateFilter filter;

    TEST_ASSERT_EQUAL(BatteryStatus::Usb, filter.update(100, false, kVbusMv).status);
    // Two unreadable samples interrupt confirmation instead of coasting on
    // the cached median.
    TEST_ASSERT_EQUAL(BatteryStatus::Usb, filter.update(-1, false, kVbusMv).status);
    TEST_ASSERT_EQUAL(BatteryStatus::Usb, filter.update(-1, false, kVbusMv).status);
    TEST_ASSERT_EQUAL(BatteryStatus::Usb, filter.update(100, false, kVbusMv).status);
    TEST_ASSERT_EQUAL(BatteryStatus::Usb, filter.update(100, false, kVbusMv).status);
    TEST_ASSERT_EQUAL(BatteryStatus::Full, filter.update(100, false, kVbusMv).status);
}

TEST_CASE("levels clamp into the display range", "[battery_view]")
{
    BatteryStateFilter filter;
    BatteryView view{};
    for (int i = 0; i < 3; ++i) view = filter.update(150, false, kNoVbusMv);
    TEST_ASSERT_EQUAL_UINT8(100, view.percent);
    TEST_ASSERT_EQUAL_UINT8(4, view.band);
}

TEST_CASE("median rejects single-sample voltage tears", "[battery_view]")
{
    BatteryStateFilter filter;
    for (int i = 0; i < 4; ++i) filter.update(60, false, kNoVbusMv);

    const auto torn_low = filter.update(5, false, kNoVbusMv);
    TEST_ASSERT_EQUAL_UINT8(60, torn_low.percent);
    TEST_ASSERT_EQUAL_UINT8(3, torn_low.band);

    const auto recovered = filter.update(60, false, kNoVbusMv);
    TEST_ASSERT_EQUAL_UINT8(60, recovered.percent);
}

TEST_CASE("a sustained level change moves the median within three samples",
          "[battery_view]")
{
    BatteryStateFilter filter;
    for (int i = 0; i < 5; ++i) filter.update(60, false, kNoVbusMv);

    filter.update(10, false, kNoVbusMv);
    filter.update(10, false, kNoVbusMv);
    const auto settled_low = filter.update(10, false, kNoVbusMv);
    TEST_ASSERT_EQUAL_UINT8(10, settled_low.percent);
}

TEST_CASE("bands follow quartile thresholds on a settled filter", "[battery_view]")
{
    const auto full = settled(90, false, 5);
    TEST_ASSERT_EQUAL_UINT8(4, full.band);
    const auto high = settled(60, false, 5);
    TEST_ASSERT_EQUAL_UINT8(3, high.band);
    const auto mid = settled(30, false, 5);
    TEST_ASSERT_EQUAL_UINT8(2, mid.band);
    const auto low = settled(10, false, 5);
    TEST_ASSERT_EQUAL_UINT8(1, low.band);
}

TEST_CASE("band flips require sustained evidence", "[battery_view]")
{
    BatteryStateFilter filter;
    BatteryView view{};
    for (int i = 0; i < 5; ++i) view = filter.update(60, false, kNoVbusMv);
    TEST_ASSERT_EQUAL_UINT8(3, view.band);

    // A short burst of off readings (torn register reads arrive in clusters)
    // must not walk the segments; the band only moves after five agreeing
    // samples.
    for (int i = 0; i < 4; ++i) {
        view = filter.update(90, false, kNoVbusMv);
        TEST_ASSERT_EQUAL_UINT8(3, view.band);
    }
    view = filter.update(90, false, kNoVbusMv);
    TEST_ASSERT_EQUAL_UINT8(4, view.band);

    for (int i = 0; i < 5; ++i) view = filter.update(15, false, kNoVbusMv);
    TEST_ASSERT_EQUAL_UINT8(1, view.band);
}

TEST_CASE("full requires three consecutive usb samples without charging", "[battery_view]")
{
    BatteryStateFilter filter;

    // Median settles instantly on identical samples; two land on Usb and the
    // third consecutive 100% sample confirms Full.
    TEST_ASSERT_EQUAL(BatteryStatus::Usb, filter.update(100, false, kVbusMv).status);
    TEST_ASSERT_EQUAL(BatteryStatus::Usb, filter.update(100, false, kVbusMv).status);
    TEST_ASSERT_EQUAL(BatteryStatus::Full, filter.update(100, false, kVbusMv).status);
    TEST_ASSERT_EQUAL(BatteryStatus::Usb, filter.update(99, false, kVbusMv).status);
}

TEST_CASE("charge blips do not break the full verdict", "[battery_view]")
{
    BatteryStateFilter filter;
    BatteryView view{};
    for (int i = 0; i < 5; ++i) view = filter.update(100, false, kVbusMv);
    TEST_ASSERT_EQUAL(BatteryStatus::Full, view.status);

    // Single-sample pin bounce must not flap the icon between Full and Charging.
    view = filter.update(100, true, kVbusMv);
    TEST_ASSERT_EQUAL(BatteryStatus::Full, view.status);
    view = filter.update(100, false, kVbusMv);
    TEST_ASSERT_EQUAL(BatteryStatus::Full, view.status);
}
