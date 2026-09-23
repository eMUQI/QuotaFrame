#include "usage_panel_state/screensaver_usage.hpp"
#include "unity.h"

using namespace usage_panel;
using namespace usage_panel;

namespace {
UsageUpdate full_update(
    Provider provider, uint32_t sent_at, uint8_t short_percent,
    uint8_t week_percent)
{
    UsageUpdate update{};
    update.provider = provider;
    update.state = SourceState::Ok;
    update.sampled_at = sent_at;
    update.sent_at = sent_at;
    update.short_window.present = true;
    update.short_window.used_percent = short_percent;
    update.week_window.present = true;
    update.week_window.used_percent = week_percent;
    return update;
}

UsageUpdate partial_update(
    Provider provider, uint32_t sent_at, uint8_t week_percent)
{
    UsageUpdate update{};
    update.provider = provider;
    update.state = SourceState::Partial;
    update.sampled_at = sent_at;
    update.sent_at = sent_at;
    update.week_window.present = true;
    update.week_window.used_percent = week_percent;
    return update;
}

UsageUpdate unavailable_update(Provider provider, uint32_t sent_at)
{
    UsageUpdate update{};
    update.provider = provider;
    update.state = SourceState::Unavailable;
    update.sampled_at = sent_at;
    update.sent_at = sent_at;
    return update;
}
}  // namespace

TEST_CASE("screensaver level rises only at the design thresholds",
          "[panel_screensaver_usage]")
{
    TEST_ASSERT_EQUAL(
        int(ScreensaverUsageLevel::Base), int(screensaver_usage_level(0)));
    TEST_ASSERT_EQUAL(
        int(ScreensaverUsageLevel::Base), int(screensaver_usage_level(79)));
    TEST_ASSERT_EQUAL(
        int(ScreensaverUsageLevel::Warn), int(screensaver_usage_level(80)));
    TEST_ASSERT_EQUAL(
        int(ScreensaverUsageLevel::Warn), int(screensaver_usage_level(94)));
    TEST_ASSERT_EQUAL(
        int(ScreensaverUsageLevel::Bad), int(screensaver_usage_level(95)));
    TEST_ASSERT_EQUAL(
        int(ScreensaverUsageLevel::Bad), int(screensaver_usage_level(100)));
}

TEST_CASE("a row prefers the short window even at zero usage",
          "[panel_screensaver_usage]")
{
    UsageModel model;
    TEST_ASSERT_TRUE(model.apply(full_update(Provider::Codex, 1000, 0, 15), 0));

    const auto row = make_screensaver_usage_row(model, Provider::Codex, true);

    TEST_ASSERT_TRUE(row.has_data);
    TEST_ASSERT_EQUAL_UINT8(0, row.percent);
    TEST_ASSERT_FALSE(row.show_percent);
    TEST_ASSERT_EQUAL(int(ScreensaverUsageLevel::Base), int(row.level));
}

TEST_CASE("a tight weekly window does not override the short window",
          "[panel_screensaver_usage]")
{
    UsageModel model;
    TEST_ASSERT_TRUE(model.apply(full_update(Provider::Claude, 1000, 19, 91), 0));

    const auto row = make_screensaver_usage_row(model, Provider::Claude, true);

    TEST_ASSERT_TRUE(row.has_data);
    TEST_ASSERT_EQUAL_UINT8(19, row.percent);
    TEST_ASSERT_FALSE(row.show_percent);
    TEST_ASSERT_EQUAL(int(ScreensaverUsageLevel::Base), int(row.level));
}

TEST_CASE("a Codex weekly-only sample fills its fallback window",
          "[panel_screensaver_usage]")
{
    UsageModel model;
    TEST_ASSERT_TRUE(model.apply(partial_update(Provider::Codex, 1000, 61), 0));

    const auto row = make_screensaver_usage_row(model, Provider::Codex, true);

    TEST_ASSERT_TRUE(row.has_data);
    TEST_ASSERT_EQUAL_UINT8(61, row.percent);
    TEST_ASSERT_FALSE(row.show_percent);
}

TEST_CASE("a partial sample ignores retained windows from an older publication",
          "[panel_screensaver_usage]")
{
    UsageModel model;
    TEST_ASSERT_TRUE(model.apply(full_update(Provider::Claude, 1000, 99, 60), 0));
    TEST_ASSERT_TRUE(model.apply(partial_update(Provider::Claude, 1010, 20), 10));

    const auto row = make_screensaver_usage_row(model, Provider::Claude, true);

    TEST_ASSERT_TRUE(row.has_data);
    TEST_ASSERT_EQUAL_UINT8(20, row.percent);
    TEST_ASSERT_FALSE(row.show_percent);
    TEST_ASSERT_EQUAL(int(ScreensaverUsageLevel::Base), int(row.level));
}

TEST_CASE("the percentage appears only from 80 percent",
          "[panel_screensaver_usage]")
{
    UsageModel model;
    TEST_ASSERT_TRUE(model.apply(full_update(Provider::Codex, 1000, 79, 0), 0));
    TEST_ASSERT_FALSE(
        make_screensaver_usage_row(model, Provider::Codex, true)
            .show_percent);

    TEST_ASSERT_TRUE(model.apply(full_update(Provider::Codex, 1000, 80, 0), 0));
    const auto warn = make_screensaver_usage_row(model, Provider::Codex, true);
    TEST_ASSERT_TRUE(warn.show_percent);
    TEST_ASSERT_EQUAL_UINT8(80, warn.percent);
    TEST_ASSERT_EQUAL(int(ScreensaverUsageLevel::Warn), int(warn.level));

    TEST_ASSERT_TRUE(model.apply(full_update(Provider::Codex, 1000, 96, 0), 0));
    const auto bad = make_screensaver_usage_row(model, Provider::Codex, true);
    TEST_ASSERT_TRUE(bad.show_percent);
    TEST_ASSERT_EQUAL(int(ScreensaverUsageLevel::Bad), int(bad.level));
}

TEST_CASE("a disconnected link clears both rows and marks offline",
          "[panel_screensaver_usage]")
{
    UsageModel model;
    TEST_ASSERT_TRUE(model.apply(full_update(Provider::Codex, 1000, 88, 0), 0));
    TEST_ASSERT_TRUE(model.apply(full_update(Provider::Claude, 1000, 96, 0), 0));

    const auto view = make_screensaver_usage_view(model, false);

    TEST_ASSERT_FALSE(view.rows[0].has_data);
    TEST_ASSERT_FALSE(view.rows[1].has_data);
    TEST_ASSERT_FALSE(view.rows[0].show_percent);
    TEST_ASSERT_FALSE(view.rows[1].show_percent);
    TEST_ASSERT_TRUE(view.offline);
}

TEST_CASE("sample age preserves its row", "[panel_screensaver_usage]")
{
    UsageModel model;
    TEST_ASSERT_TRUE(model.apply(full_update(Provider::Codex, 1000, 42, 0), 0));

    auto republished = full_update(Provider::Codex, 1000, 42, 0);
    republished.sent_at = 100000;
    TEST_ASSERT_TRUE(model.apply(republished, 99000000));
    const auto row = make_screensaver_usage_row(model, Provider::Codex, true);
    TEST_ASSERT_TRUE(row.has_data);
    TEST_ASSERT_EQUAL_UINT8(42, row.percent);
}

TEST_CASE("a source failure preserves its row", "[panel_screensaver_usage]")
{
    UsageModel model;
    TEST_ASSERT_TRUE(model.apply(full_update(Provider::Codex, 1000, 42, 0), 0));
    TEST_ASSERT_TRUE(model.apply(unavailable_update(Provider::Codex, 1000), 0));

    TEST_ASSERT_TRUE(
        make_screensaver_usage_row(model, Provider::Codex, true)
            .has_data);
}

TEST_CASE("one silent provider does not degrade the other",
          "[panel_screensaver_usage]")
{
    UsageModel model;
    TEST_ASSERT_TRUE(model.apply(full_update(Provider::Codex, 1000, 42, 0), 0));

    const auto view = make_screensaver_usage_view(model, true);

    TEST_ASSERT_TRUE(view.rows[0].has_data);
    TEST_ASSERT_EQUAL_UINT8(42, view.rows[0].percent);
    TEST_ASSERT_FALSE(view.rows[1].has_data);
    TEST_ASSERT_FALSE(view.offline);
}

TEST_CASE("one row's number brings out the other row's number",
          "[panel_screensaver_usage]")
{
    UsageModel model;
    TEST_ASSERT_TRUE(model.apply(full_update(Provider::Codex, 1000, 91, 19), 0));
    TEST_ASSERT_TRUE(model.apply(full_update(Provider::Claude, 1000, 30, 10), 0));

    const auto view = make_screensaver_usage_view(model, true);

    // Equal ink widths keep the two underlines aligned and centred.
    TEST_ASSERT_TRUE(view.rows[0].show_percent);
    TEST_ASSERT_TRUE(view.rows[1].show_percent);
    // Severity stays per row: only the alerting row brightens.
    TEST_ASSERT_EQUAL(int(ScreensaverUsageLevel::Warn), int(view.rows[0].level));
    TEST_ASSERT_EQUAL(int(ScreensaverUsageLevel::Base), int(view.rows[1].level));
    TEST_ASSERT_EQUAL_UINT8(91, view.rows[0].percent);
    TEST_ASSERT_EQUAL_UINT8(30, view.rows[1].percent);
}

TEST_CASE("a row without data gains no number from the other row",
          "[panel_screensaver_usage]")
{
    UsageModel model;
    TEST_ASSERT_TRUE(model.apply(full_update(Provider::Codex, 1000, 96, 0), 0));

    const auto view = make_screensaver_usage_view(model, true);

    TEST_ASSERT_TRUE(view.rows[0].show_percent);
    TEST_ASSERT_FALSE(view.rows[1].has_data);
    TEST_ASSERT_FALSE(view.rows[1].show_percent);
}

TEST_CASE("an empty model reads as offline", "[panel_screensaver_usage]")
{
    UsageModel model;
    const auto view = make_screensaver_usage_view(model, true);

    TEST_ASSERT_FALSE(view.rows[0].has_data);
    TEST_ASSERT_FALSE(view.rows[1].has_data);
    TEST_ASSERT_TRUE(view.offline);
}
