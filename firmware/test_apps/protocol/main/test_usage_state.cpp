#include "unity.h"
#include "usage_core/usage_state.hpp"

using namespace usage_panel;

static UsageUpdate valid_update()
{
    UsageUpdate update{};
    update.provider = Provider::Codex;
    update.state = SourceState::Ok;
    update.sampled_at = 1000;
    update.sent_at = 1002;
    update.short_window = {true, 36, true, 2000};
    update.week_window = {true, 61, true, 5000};
    return update;
}

TEST_CASE("offline retains the last valid values", "[usage]")
{
    UsageModel model;
    TEST_ASSERT_TRUE(model.apply(valid_update(), 1000));
    TEST_ASSERT_EQUAL_UINT8(36, model.snapshot(Provider::Codex).short_window.used_percent);
    TEST_ASSERT_EQUAL(int(DisplayState::Offline),
                      int(model.display_state(Provider::Codex, false)));
}

TEST_CASE("unavailable preserves the complete valid sample and normal display", "[usage]")
{
    UsageModel model;
    TEST_ASSERT_TRUE(model.apply(valid_update(), 1000));
    UsageUpdate error{};
    error.provider = Provider::Codex;
    error.state = SourceState::Unavailable;
    error.sampled_at = 1100;
    error.sent_at = 1100;
    TEST_ASSERT_TRUE(model.apply(error, 2000));
    TEST_ASSERT_EQUAL_UINT8(36, model.snapshot(Provider::Codex).short_window.used_percent);
    TEST_ASSERT_EQUAL_UINT32(1000, model.snapshot(Provider::Codex).sampled_at);
    TEST_ASSERT_TRUE(model.snapshot(Provider::Codex).latest_short_present);
    TEST_ASSERT_TRUE(model.snapshot(Provider::Codex).latest_week_present);
    TEST_ASSERT_EQUAL_UINT32(1105, model.estimated_epoch(Provider::Codex, 7000));
    TEST_ASSERT_EQUAL(int(DisplayState::Online),
                      int(model.display_state(Provider::Codex, true)));
}

TEST_CASE("sample age does not invalidate connected data", "[usage]")
{
    UsageModel model;
    auto update = valid_update();
    update.sampled_at = 1000;
    update.sent_at = 1190;
    TEST_ASSERT_TRUE(model.apply(update, 5000));
    TEST_ASSERT_EQUAL(int(DisplayState::Online),
                      int(model.display_state(Provider::Codex, true)));
}

TEST_CASE("clock skew validation does not wrap near uint32 max", "[usage]")
{
    auto update = valid_update();
    update.sampled_at = 1000;
    update.sent_at = UINT32_MAX - 100;

    UsageModel model;
    TEST_ASSERT_TRUE(model.apply(update, 5000));
}

TEST_CASE("countdown formatting covers all ranges", "[usage]")
{
    char text[16]{};
    format_countdown(false, 0, 0, text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("--", text);
    format_countdown(true, 100, 100, text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("WAIT", text);
    format_countdown(true, 42 * 60, 0, text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("42m", text);
    format_countdown(true, 18480, 0, text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("5h 08m", text);
    format_countdown(true, 284400, 0, text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("3d 07h", text);
}

TEST_CASE("initial failure stays empty and partial failures preserve window selection", "[usage]")
{
    UsageModel model;
    UsageUpdate unavailable{};
    unavailable.sent_at = 1000;
    unavailable.sampled_at = 1000;
    TEST_ASSERT_TRUE(model.apply(unavailable, 0));
    TEST_ASSERT_EQUAL(int(DisplayState::NoData),
                      int(model.display_state(Provider::Codex, true)));

    TEST_ASSERT_TRUE(model.apply(valid_update(), 0));
    auto partial = valid_update();
    partial.state = SourceState::Partial;
    partial.short_window = {};
    TEST_ASSERT_TRUE(model.apply(partial, 0));
    TEST_ASSERT_TRUE(model.apply(unavailable, 0));
    const auto& snapshot = model.snapshot(Provider::Codex);
    TEST_ASSERT_FALSE(snapshot.latest_short_present);
    TEST_ASSERT_TRUE(snapshot.latest_week_present);
    TEST_ASSERT_EQUAL(int(DisplayState::Partial),
                      int(model.display_state(Provider::Codex, true)));
    TEST_ASSERT_EQUAL(int(DisplayState::Offline),
                      int(model.display_state(Provider::Codex, false)));
    TEST_ASSERT_TRUE(model.apply(valid_update(), 86400000));
    TEST_ASSERT_EQUAL(int(DisplayState::Online),
                      int(model.display_state(Provider::Codex, true)));
}
