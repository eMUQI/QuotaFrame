#include "usage_panel_state/render_schedule.hpp"
#include "unity.h"

#include <cstring>

using namespace usage_panel;
using namespace usage_panel;

namespace {
UsageUpdate partial_update(
    Provider provider, uint32_t sent_at, uint32_t reset_at)
{
    UsageUpdate update{};
    update.provider = provider;
    update.state = SourceState::Partial;
    update.sampled_at = sent_at;
    update.sent_at = sent_at;
    update.short_window.present = true;
    update.short_window.used_percent = 25;
    update.short_window.has_reset = true;
    update.short_window.reset_at = reset_at;
    return update;
}
}

TEST_CASE("unchanged AMOLED render inputs produce the same key", "[panel_render]")
{
    UsageModel model;
    const auto first = make_timed_render_key(model, Page::Overview, false, 1000);
    const auto second = make_timed_render_key(model, Page::Overview, false, 1500);

    TEST_ASSERT_TRUE(first == second);
    TEST_ASSERT_EQUAL(int(DisplayState::NoData), int(first.first_state));
    TEST_ASSERT_EQUAL(int(DisplayState::NoData), int(first.second_state));
    TEST_ASSERT_EQUAL_STRING("", first.short_countdown);
    TEST_ASSERT_EQUAL_STRING("", first.week_countdown);
}

TEST_CASE("page selection changes the render key", "[panel_render]")
{
    UsageModel model;
    const auto overview = make_timed_render_key(model, Page::Overview, false, 0);
    const auto codex = make_timed_render_key(model, Page::Codex, false, 0);

    TEST_ASSERT_TRUE(overview != codex);
}

TEST_CASE("provider countdown changes only when visible text changes", "[panel_render]")
{
    UsageModel model;
    const auto update = partial_update(Provider::Codex, 1000, 1061);
    TEST_ASSERT_TRUE(model.apply(update, 0));

    const auto first = make_timed_render_key(model, Page::Codex, true, 0);
    const auto same_text = make_timed_render_key(model, Page::Codex, true, 500);
    const auto next_text = make_timed_render_key(model, Page::Codex, true, 1500);

    TEST_ASSERT_TRUE(first == same_text);
    TEST_ASSERT_TRUE(first != next_text);
    TEST_ASSERT_EQUAL_STRING("2m", first.short_countdown);
    TEST_ASSERT_EQUAL_STRING("1m", next_text.short_countdown);
}

TEST_CASE("overview hides countdowns for non-online providers", "[panel_render]")
{
    UsageModel model;
    TEST_ASSERT_TRUE(model.apply(
        partial_update(Provider::Codex, 1000, 1061), 0));

    const auto key = make_timed_render_key(model, Page::Overview, true, 0);

    TEST_ASSERT_EQUAL_STRING("", key.overview_countdowns[0]);
    TEST_ASSERT_EQUAL_STRING("", key.overview_countdowns[1]);
    TEST_ASSERT_EQUAL(int(DisplayState::Partial), int(key.first_state));
    TEST_ASSERT_EQUAL(int(DisplayState::NoData), int(key.second_state));
    TEST_ASSERT_EQUAL_STRING("", key.short_countdown);
    TEST_ASSERT_EQUAL_STRING("", key.week_countdown);
}

TEST_CASE("overview redraws at visible countdown boundaries for either provider",
          "[panel_render]")
{
    const Provider providers[] = {Provider::Codex, Provider::Claude};
    for (size_t i = 0; i < 2; ++i) {
        UsageModel model;
        auto update = partial_update(providers[i], 1000, 1061);
        update.state = SourceState::Ok;
        update.week_window = {true, 50, true, 9000};
        TEST_ASSERT_TRUE(model.apply(update, 0));

        const auto first = make_timed_render_key(model, Page::Overview, true, 0);
        const auto unchanged = make_timed_render_key(model, Page::Overview, true, 500);
        const auto minute = make_timed_render_key(model, Page::Overview, true, 1500);
        const auto due = make_timed_render_key(model, Page::Overview, true, 61000);
        TEST_ASSERT_TRUE(first == unchanged);
        TEST_ASSERT_TRUE(first != minute);
        TEST_ASSERT_TRUE(minute != due);
        TEST_ASSERT_EQUAL_STRING("2m", first.overview_countdowns[i]);
        TEST_ASSERT_EQUAL_STRING("1m", minute.overview_countdowns[i]);
        TEST_ASSERT_EQUAL_STRING("WAIT", due.overview_countdowns[i]);
        TEST_ASSERT_EQUAL_STRING("", first.overview_countdowns[1 - i]);

        PanelPresentation presentation{};
        presentation.screensaver.active = true;
        TEST_ASSERT_TRUE(
            make_timed_render_key(model, Page::Overview, true, 0, presentation) ==
            make_timed_render_key(model, Page::Overview, true, 61000, presentation));
        TEST_ASSERT_TRUE(
            make_timed_render_key(model, Page::Overview, false, 0) ==
            make_timed_render_key(model, Page::Overview, false, 61000));
    }
}

TEST_CASE("active screensaver tracks both providers from a detail page",
          "[panel_render]")
{
    UsageModel model;
    TEST_ASSERT_TRUE(model.apply(
        partial_update(Provider::Claude, 1000, 1061), 0));

    PanelPresentation presentation{};
    presentation.screensaver.active = true;
    const auto fresh = make_timed_render_key(model, Page::Codex, true, 180000, presentation);
    const auto stale = make_timed_render_key(model, Page::Codex, true, 181000, presentation);

    TEST_ASSERT_EQUAL(int(DisplayState::NoData), int(fresh.first_state));
    TEST_ASSERT_EQUAL(int(DisplayState::Partial), int(fresh.second_state));
    TEST_ASSERT_EQUAL(int(DisplayState::Partial), int(stale.second_state));
    TEST_ASSERT_TRUE(fresh == stale);
    TEST_ASSERT_EQUAL_STRING("", fresh.short_countdown);
    TEST_ASSERT_EQUAL_STRING("", fresh.week_countdown);
}

TEST_CASE("render key tracks only visible battery and presentation changes", "[panel_render]")
{
    UsageModel model;
    PanelPresentation first_presentation{};
    first_presentation.battery = {
        .available = true,
        .band = BatteryBand::Two,
        .percent = 25,
    };
    PanelPresentation same_band = first_presentation;
    same_band.battery.percent = 49;

    const auto first = make_timed_render_key(model, Page::Overview, false, 0, first_presentation);
    const auto unchanged = make_timed_render_key(model, Page::Overview, false, 0, same_band);
    TEST_ASSERT_TRUE(first == unchanged);

    same_band.clock.available = true;
    std::memcpy(same_band.clock.time, "12:34", 6);
    const auto changed_clock = make_timed_render_key(model, Page::Overview, false, 0, same_band);
    TEST_ASSERT_TRUE(first != changed_clock);

    same_band = first_presentation;
    same_band.screensaver.active = true;
    const auto changed_screensaver = make_timed_render_key(model, Page::Overview, false, 0, same_band);
    TEST_ASSERT_TRUE(first != changed_screensaver);
}
