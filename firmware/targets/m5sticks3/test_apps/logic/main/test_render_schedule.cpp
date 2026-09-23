#include "unity.h"

#include "render_schedule.hpp"

using namespace usage_panel;
namespace {

UsageUpdate timed_update(Provider provider)
{
    UsageUpdate update{};
    update.provider = provider;
    update.state = SourceState::Ok;
    update.sampled_at = 1000;
    update.sent_at = 1000;
    update.short_window = {true, 25, true, 1120};
    update.week_window = {true, 50, true, 4600};
    return update;
}

}  // namespace

TEST_CASE("provider key stays equal while visible countdown text is unchanged",
          "[render_schedule]")
{
    UsageModel model;
    TEST_ASSERT_TRUE(model.apply(timed_update(Provider::Codex), 0));

    const auto first = make_timed_render_key(model, Page::Codex, true, 1000, usage_panel::m5::BatteryView{});
    const auto second = make_timed_render_key(model, Page::Codex, true, 2000, usage_panel::m5::BatteryView{});

    TEST_ASSERT_TRUE(first == second);
    TEST_ASSERT_EQUAL_STRING("2m", first.short_countdown);
}

TEST_CASE("provider key changes when visible countdown text changes",
          "[render_schedule]")
{
    UsageModel model;
    TEST_ASSERT_TRUE(model.apply(timed_update(Provider::Codex), 0));

    const auto two_minutes = make_timed_render_key(
        model, Page::Codex, true, 0, usage_panel::m5::BatteryView{});
    const auto one_minute = make_timed_render_key(
        model, Page::Codex, true, 60000, usage_panel::m5::BatteryView{});

    TEST_ASSERT_TRUE(two_minutes != one_minute);
    TEST_ASSERT_EQUAL_STRING("2m", two_minutes.short_countdown);
    TEST_ASSERT_EQUAL_STRING("1m", one_minute.short_countdown);
}

TEST_CASE("overview state remains online as a sample ages",
          "[render_schedule]")
{
    UsageModel model;
    TEST_ASSERT_TRUE(model.apply(timed_update(Provider::Codex), 0));

    const auto fresh = make_timed_render_key(
        model, Page::Overview, true, 180000, usage_panel::m5::BatteryView{});
    const auto stale = make_timed_render_key(
        model, Page::Overview, true, 181000, usage_panel::m5::BatteryView{});

    TEST_ASSERT_TRUE(fresh == stale);
    TEST_ASSERT_EQUAL(int(DisplayState::Online), int(fresh.first_state));
    TEST_ASSERT_EQUAL(int(DisplayState::Online), int(stale.first_state));
    TEST_ASSERT_EQUAL(int(DisplayState::NoData), int(stale.second_state));
}

TEST_CASE("no-data provider key ignores monotonic time",
          "[render_schedule]")
{
    UsageModel model;

    const auto first = make_timed_render_key(model, Page::Claude, true, 0, usage_panel::m5::BatteryView{});
    const auto second = make_timed_render_key(
        model, Page::Claude, true, UINT64_MAX, usage_panel::m5::BatteryView{});

    TEST_ASSERT_TRUE(first == second);
    TEST_ASSERT_EQUAL(int(DisplayState::NoData), int(first.first_state));
    TEST_ASSERT_EQUAL_STRING("", first.short_countdown);
    TEST_ASSERT_EQUAL_STRING("", first.week_countdown);
}

TEST_CASE("render key changes when OTA confirmation countdown changes",
          "[render_schedule]")
{
    TimedRenderKey first{};
    TimedRenderKey second{};
    first.ota_confirm_seconds = 60;
    second.ota_confirm_seconds = 59;

    TEST_ASSERT_TRUE(first != second);
}

TEST_CASE("render key tracks visible battery state", "[render_schedule]")
{
    UsageModel model;

    const usage_panel::m5::BatteryView hidden{};
    const auto without_battery =
        make_timed_render_key(model, Page::Overview, true, 0, hidden);

    usage_panel::m5::BatteryView battery{};
    battery.available = true;
    battery.status = usage_panel::m5::BatteryStatus::Charging;
    battery.percent = 60;
    battery.band = 3;
    const auto with_battery =
        make_timed_render_key(model, Page::Overview, true, 0, battery);
    const auto unchanged =
        make_timed_render_key(model, Page::Overview, true, 0, battery);

    TEST_ASSERT_TRUE(without_battery != with_battery);
    TEST_ASSERT_TRUE(with_battery == unchanged);

    ++battery.band;
    TEST_ASSERT_TRUE(
        with_battery != make_timed_render_key(model, Page::Overview, true, 0, battery));

    // The icon never renders the percentage, so jitter inside one band must
    // not invalidate the render key (that would redraw every second).
    usage_panel::m5::BatteryView jittered = battery;
    jittered.percent = 41;
    TEST_ASSERT_TRUE(battery.percent != jittered.percent);
    TEST_ASSERT_TRUE(
        make_timed_render_key(model, Page::Overview, true, 0, battery) ==
        make_timed_render_key(model, Page::Overview, true, 0, jittered));
}
