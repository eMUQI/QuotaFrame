#include "panel_logic.hpp"
#include "unity.h"

using namespace usage_panel;
using namespace usage_panel::rlcd;

namespace {
constexpr uint32_t kNow = 1790253127;

UsageWindow window(uint8_t used, uint32_t remaining) { return {true, used, true, kNow + remaining}; }

View linked_view() {
    View v;
    v.link = {true, true, false, 0};
    return v;
}

void publish(View &v, Provider p, uint8_t short_used) {
    UsageUpdate u;
    u.provider = p;
    u.state = SourceState::Ok;
    u.sampled_at = kNow;
    u.sent_at = kNow;
    u.short_window = window(short_used, 3600);
    u.week_window = window(10, 86400);
    v.model.apply(u, 0);
}
} // namespace

TEST_CASE("pace compares usage with the elapsed window", "[pace]")
{
    // With 2:14:05 remaining in a five-hour window, 55 % of the time has elapsed.
    Pace p = compute_pace(window(42, 8045), kNow, kShortWindowSeconds);
    TEST_ASSERT_TRUE(p.valid);
    TEST_ASSERT_EQUAL_INT(-13, p.delta);
    p = compute_pace(window(84, 5045), kNow, kShortWindowSeconds);
    TEST_ASSERT_EQUAL_INT(12, p.delta);
    TEST_ASSERT_EQUAL_UINT32(12955, p.elapsed_s);
    p = compute_pace(window(61, 439320), kNow, kWeekWindowSeconds);
    TEST_ASSERT_EQUAL_INT(34, p.delta);
    p = compute_pace(window(18, 281520), kNow, kWeekWindowSeconds);
    TEST_ASSERT_EQUAL_INT(-35, p.delta);
}

TEST_CASE("pace is invalid without a usable reset", "[pace]")
{
    UsageWindow w = window(50, 100);
    w.has_reset = false;
    TEST_ASSERT_FALSE(compute_pace(w, kNow, kShortWindowSeconds).valid);
    TEST_ASSERT_FALSE(compute_pace(window(50, 0), kNow, kShortWindowSeconds).valid);
    TEST_ASSERT_FALSE(compute_pace(window(50, kShortWindowSeconds + 1), kNow, kShortWindowSeconds).valid);
    TEST_ASSERT_FALSE(compute_pace(window(50, 100), 0, kShortWindowSeconds).valid);
}

TEST_CASE("countdowns follow the design formats", "[format]")
{
    char b[32];
    format_short_countdown(window(0, 8045), kNow, true, b, sizeof(b));
    TEST_ASSERT_EQUAL_STRING("2:14:05", b);
    format_short_countdown(window(0, 8045), kNow, false, b, sizeof(b));
    TEST_ASSERT_EQUAL_STRING("2:14", b);
    format_week_countdown(window(0, 439320), kNow, b, sizeof(b));
    TEST_ASSERT_EQUAL_STRING("5D 02:02", b);
    format_short_countdown(window(0, 0), kNow, true, b, sizeof(b));
    TEST_ASSERT_EQUAL_STRING("WAIT", b);
    UsageWindow none{};
    format_week_countdown(none, kNow, b, sizeof(b));
    TEST_ASSERT_EQUAL_STRING("--", b);
    format_elapsed(12955, b, sizeof(b));
    TEST_ASSERT_EQUAL_STRING("3H35M", b);
    format_elapsed(2 * 86400 + 4 * 3600 + 59 * 60, b, sizeof(b));
    TEST_ASSERT_EQUAL_STRING("2D 04H", b);
    format_age(59, b, sizeof(b));
    TEST_ASSERT_EQUAL_STRING("NOW", b);
    format_age(120, b, sizeof(b));
    TEST_ASSERT_EQUAL_STRING("2M AGO", b);
    format_age(3 * 3600, b, sizeof(b));
    TEST_ASSERT_EQUAL_STRING("3H AGO", b);
}

TEST_CASE("the landscape ring and the portrait ring cycle in both directions", "[nav]")
{
    TEST_ASSERT_EQUAL_UINT8(uint8_t(Page::Codex), uint8_t(Navigator::next(Page::Home, false)));
    TEST_ASSERT_EQUAL_UINT8(uint8_t(Page::Claude), uint8_t(Navigator::next(Page::Codex, false)));
    TEST_ASSERT_EQUAL_UINT8(uint8_t(Page::Trend), uint8_t(Navigator::next(Page::Claude, false)));
    TEST_ASSERT_EQUAL_UINT8(uint8_t(Page::Clock), uint8_t(Navigator::next(Page::Trend, false)));
    TEST_ASSERT_EQUAL_UINT8(uint8_t(Page::Home), uint8_t(Navigator::next(Page::Clock, false)));
    TEST_ASSERT_EQUAL_UINT8(uint8_t(Page::Clock), uint8_t(Navigator::next(Page::Home, true)));
    TEST_ASSERT_EQUAL_UINT8(uint8_t(Page::Home), uint8_t(Navigator::next(Page::Clock, true)));
    TEST_ASSERT_EQUAL_UINT8(uint8_t(Page::Clock), uint8_t(Navigator::previous(Page::Home, false)));
    TEST_ASSERT_EQUAL_UINT8(uint8_t(Page::Home), uint8_t(Navigator::previous(Page::Codex, false)));
    TEST_ASSERT_EQUAL_UINT8(uint8_t(Page::Clock), uint8_t(Navigator::previous(Page::Home, true)));
    for (Page page : {Page::Codex, Page::Claude, Page::Trend, Page::Settings})
        TEST_ASSERT_EQUAL_UINT8(uint8_t(Page::Home), uint8_t(Navigator::previous(page, true)));
    TEST_ASSERT_EQUAL_UINT8(uint8_t(Page::Home), uint8_t(Navigator::previous(Page::Settings, false)));
}

TEST_CASE("KEY steps to the previous page and BOOT to the next", "[nav]")
{
    Navigator nav;
    View v = linked_view();
    nav.input(Input::Key, v, 0);
    TEST_ASSERT_EQUAL_UINT8(uint8_t(Page::Clock), uint8_t(v.page));
    nav.input(Input::Boot, v, 0);
    nav.input(Input::Boot, v, 0);
    TEST_ASSERT_EQUAL_UINT8(uint8_t(Page::Codex), uint8_t(v.page));
}

TEST_CASE("settings open on hold, cycle values and return to the previous page", "[nav]")
{
    Navigator nav;
    View v = linked_view();
    v.page = Page::Claude;
    nav.input(Input::KeyHold, v, 0);
    TEST_ASSERT_EQUAL_UINT8(uint8_t(Page::Settings), uint8_t(v.page));
    TEST_ASSERT_EQUAL_INT(0, v.focus);
    publish(v, Provider::Codex, 96);
    v.warning = {2, 0};
    TEST_ASSERT_TRUE(nav.update_alert(v));
    TEST_ASSERT_TRUE(nav.input(Input::Boot, v, 0).save);
    TEST_ASSERT_EQUAL_UINT8(1, v.settings.cycle);
    nav.input(Input::Key, v, 0);
    nav.input(Input::Key, v, 0);
    TEST_ASSERT_EQUAL_INT(2, v.focus);
    nav.input(Input::Boot, v, 0);
    TEST_ASSERT_EQUAL_UINT8(0, v.settings.seconds);
    nav.input(Input::Key, v, 0);
    nav.input(Input::Boot, v, 0);
    TEST_ASSERT_EQUAL_UINT8(uint8_t(Rotation::Portrait), v.settings.rotation);
    nav.input(Input::KeyHold, v, 0);
    // Claude has no portrait page, so leaving Settings in portrait lands on Home.
    TEST_ASSERT_EQUAL_UINT8(uint8_t(Page::Home), uint8_t(v.page));
    TEST_ASSERT_EQUAL_INT(0, v.alert);
    nav.input(Input::Key, v, 0);
    TEST_ASSERT_EQUAL_INT(-1, v.alert);
}

TEST_CASE("settings retry a failed save without changing the value", "[nav]")
{
    Navigator nav;
    View v = linked_view();
    nav.input(Input::KeyHold, v, 0);
    nav.input(Input::Boot, v, 0);
    TEST_ASSERT_EQUAL_UINT8(1, v.settings.cycle);
    v.save_error = true;
    TEST_ASSERT_TRUE(nav.input(Input::Boot, v, 0).save);
    TEST_ASSERT_EQUAL_UINT8(1, v.settings.cycle);
}

TEST_CASE("alert raises once, dismisses and re-arms below the threshold", "[alert]")
{
    Navigator nav;
    View v = linked_view();
    publish(v, Provider::Codex, 96);
    publish(v, Provider::Claude, 97);
    v.warning = {2, 2};
    TEST_ASSERT_TRUE(nav.update_alert(v));
    TEST_ASSERT_EQUAL_INT(1, v.alert);
    TEST_ASSERT_FALSE(nav.update_alert(v));
    v.link.encrypted = false;
    TEST_ASSERT_FALSE(nav.update_alert(v));
    TEST_ASSERT_EQUAL_INT(-1, v.alert);
    v.link.encrypted = true;
    TEST_ASSERT_FALSE(nav.update_alert(v));
    TEST_ASSERT_EQUAL_INT(1, v.alert);
    nav.input(Input::Key, v, 0);
    TEST_ASSERT_TRUE(nav.update_alert(v));
    TEST_ASSERT_EQUAL_INT(0, v.alert);
    nav.input(Input::Key, v, 0);
    TEST_ASSERT_FALSE(nav.update_alert(v));
    TEST_ASSERT_EQUAL_INT(-1, v.alert);
    v.warning = {2, 1};
    nav.update_alert(v);
    v.warning = {2, 2};
    TEST_ASSERT_TRUE(nav.update_alert(v));
    TEST_ASSERT_EQUAL_INT(1, v.alert);
}

TEST_CASE("alert stays off when disabled or unlinked", "[alert]")
{
    Navigator nav;
    View v = linked_view();
    publish(v, Provider::Claude, 97);
    v.warning = {0, 2};
    v.settings.alert = uint8_t(AlertMode::Off);
    TEST_ASSERT_FALSE(nav.update_alert(v));
    v.settings.alert = uint8_t(AlertMode::Invert);
    v.link.encrypted = false;
    TEST_ASSERT_FALSE(nav.update_alert(v));
    TEST_ASSERT_EQUAL_INT(-1, v.alert);
}

TEST_CASE("auto cycle advances after the interval and pauses in settings", "[cycle]")
{
    Navigator nav;
    View v = linked_view();
    v.settings.cycle = 1;
    nav.auto_cycle(v, 0);
    TEST_ASSERT_FALSE(nav.auto_cycle(v, 29999));
    TEST_ASSERT_TRUE(nav.auto_cycle(v, 30000));
    TEST_ASSERT_EQUAL_UINT8(uint8_t(Page::Codex), uint8_t(v.page));
    v.page = Page::Settings;
    TEST_ASSERT_FALSE(nav.auto_cycle(v, 90000));
}
