#include "usage_panel_state/screensaver_state.hpp"
#include "usage_panel_state/display_settings.hpp"
#include "unity.h"

using namespace usage_panel;

TEST_CASE("screensaver enters after five minutes and preserves page", "[screensaver]")
{
    ScreensaverController controller;
    controller.reset(1000, Page::Codex);
    controller.update(300999, Page::Claude, false, false);
    TEST_ASSERT_FALSE(controller.view(300999).active);
    controller.update(301000, Page::Claude, false, false);
    TEST_ASSERT_TRUE(controller.view(301000).active);
    TEST_ASSERT_EQUAL(int(Page::Claude), int(controller.restore_page()));
}

TEST_CASE("screensaver honours an injected idle timeout", "[screensaver]")
{
    ScreensaverController controller(1000);
    controller.reset(0, Page::Codex);
    controller.update(999, Page::Claude, false, false);
    TEST_ASSERT_FALSE(controller.view(999).active);
    controller.update(1000, Page::Claude, false, false);
    TEST_ASSERT_TRUE(controller.view(1000).active);
    TEST_ASSERT_EQUAL(int(Page::Claude), int(controller.restore_page()));
}

TEST_CASE("a longer injected timeout delays screensaver entry", "[screensaver]")
{
    ScreensaverController controller(600000);
    controller.reset(0, Page::Codex);
    // Still awake well past the default five-minute delay.
    controller.update(300000, Page::Claude, false, false);
    TEST_ASSERT_FALSE(controller.view(300000).active);
    controller.update(599999, Page::Claude, false, false);
    TEST_ASSERT_FALSE(controller.view(599999).active);
    controller.update(600000, Page::Claude, false, false);
    TEST_ASSERT_TRUE(controller.view(600000).active);
}

TEST_CASE("power key locks immediately and remembers the page", "[screensaver]")
{
    ScreensaverController controller;
    controller.reset(0, Page::Overview);
    controller.note_power_key(5000, Page::Claude);

    TEST_ASSERT_TRUE(controller.view(5000).active);
    TEST_ASSERT_EQUAL(int(Page::Claude), int(controller.restore_page()));
}

TEST_CASE("power key wakes an active screensaver", "[screensaver]")
{
    ScreensaverController controller;
    controller.reset(0, Page::Overview);
    controller.update(300000, Page::Codex, false, false);
    TEST_ASSERT_TRUE(controller.view(300000).active);

    controller.note_power_key(300010, Page::Codex);
    TEST_ASSERT_FALSE(controller.view(300010).active);
}

TEST_CASE("a second power key press always toggles back", "[screensaver]")
{
    ScreensaverController controller;
    controller.reset(0, Page::Overview);
    controller.note_power_key(1000, Page::Codex);
    TEST_ASSERT_TRUE(controller.view(1000).active);

    // Inside the wake guard: the key itself is never suppressed.
    controller.note_power_key(1100, Page::Codex);
    TEST_ASSERT_FALSE(controller.view(1100).active);
}

TEST_CASE("the press motion cannot immediately undo a power key lock",
          "[screensaver]")
{
    ScreensaverController controller;
    controller.reset(0, Page::Overview);
    controller.note_power_key(1000, Page::Codex);

    controller.note_shake(1400);
    TEST_ASSERT_TRUE(controller.view(1400).active);
    controller.note_touch_down(1400);
    TEST_ASSERT_TRUE(controller.view(1400).active);

    // The guard expires 500 ms after the lock.
    controller.note_shake(1500);
    TEST_ASSERT_FALSE(controller.view(1500).active);
}

TEST_CASE("a touch wakes a power key lock once the guard expires",
          "[screensaver]")
{
    ScreensaverController controller;
    controller.reset(0, Page::Overview);
    controller.note_power_key(1000, Page::Codex);

    controller.note_touch_down(1500);
    TEST_ASSERT_FALSE(controller.view(1500).active);
    TEST_ASSERT_TRUE(controller.view(1500).consume_touch);
}

TEST_CASE("a power key lock still idles out rather than sticking", "[screensaver]")
{
    ScreensaverController controller;
    controller.reset(0, Page::Overview);
    controller.note_power_key(1000, Page::Codex);
    controller.note_power_key(1100, Page::Codex);
    TEST_ASSERT_FALSE(controller.view(1100).active);

    // Waking by key counts as activity, so the idle timer restarts from there.
    controller.update(301099, Page::Codex, false, false);
    TEST_ASSERT_FALSE(controller.view(301099).active);
    controller.update(301100, Page::Codex, false, false);
    TEST_ASSERT_TRUE(controller.view(301100).active);
}

TEST_CASE("pairing and OTA prevent or immediately exit screensaver", "[screensaver]")
{
    ScreensaverController controller;
    controller.reset(0, Page::Overview);
    controller.update(300000, Page::Codex, true, false);
    TEST_ASSERT_FALSE(controller.view(300000).active);
    controller.update(600000, Page::Codex, false, true);
    TEST_ASSERT_FALSE(controller.view(600000).active);
    controller.update(900000, Page::Codex, false, false);
    TEST_ASSERT_TRUE(controller.view(900000).active);
    controller.update(900001, Page::Codex, true, false);
    TEST_ASSERT_FALSE(controller.view(900001).active);
}

TEST_CASE("wake touch is consumed and navigation is suppressed for 300 ms", "[screensaver]")
{
    ScreensaverController controller;
    controller.reset(0, Page::Overview);
    controller.update(300000, Page::Codex, false, false);
    controller.note_touch_down(300010);

    TEST_ASSERT_FALSE(controller.view(300010).active);
    TEST_ASSERT_TRUE(controller.view(300010).consume_touch);
    TEST_ASSERT_FALSE(controller.touch_allowed(300309));
    TEST_ASSERT_TRUE(controller.touch_allowed(300310));
}

TEST_CASE("screensaver hint holds then fades over 400 ms", "[screensaver]")
{
    ScreensaverController controller;
    controller.reset(0, Page::Overview);
    controller.update(300000, Page::Overview, false, false);

    TEST_ASSERT_EQUAL_UINT8(255, controller.view(303000).hint_opacity);
    TEST_ASSERT_UINT8_WITHIN(1, 127, controller.view(303200).hint_opacity);
    TEST_ASSERT_EQUAL_UINT8(0, controller.view(303400).hint_opacity);
}

TEST_CASE("burn-in displacement changes each minute within ten pixels", "[screensaver]")
{
    ScreensaverController controller;
    controller.reset(0, Page::Overview);
    controller.update(300000, Page::Overview, false, false);
    const ScreensaverView initial = controller.view(300000);
    controller.update(360000, Page::Overview, false, false);
    const ScreensaverView shifted = controller.view(360000);

    TEST_ASSERT_TRUE(
        shifted.offset_x != initial.offset_x ||
        shifted.offset_y != initial.offset_y);
    TEST_ASSERT_INT_WITHIN(10, 0, shifted.offset_x);
    TEST_ASSERT_INT_WITHIN(10, 0, shifted.offset_y);
}

TEST_CASE("shake detector requires two paired excursions", "[screensaver]")
{
    ShakeDetector detector;
    TEST_ASSERT_FALSE(detector.update(0.0F, 0.0F, 1.0F, 0));
    TEST_ASSERT_FALSE(detector.update(2.0F, 0.0F, 0.0F, 10));
    TEST_ASSERT_FALSE(detector.update(2.0F, 0.0F, 0.0F, 200));
    TEST_ASSERT_FALSE(detector.update(2.0F, 0.0F, 0.0F, 300));
    TEST_ASSERT_FALSE(detector.update(0.0F, 0.0F, 1.0F, 500));
    TEST_ASSERT_FALSE(detector.update(2.0F, 0.0F, 0.0F, 1000));
    TEST_ASSERT_TRUE(detector.update(2.0F, 0.0F, 0.0F, 1100));
}

TEST_CASE("normal gravity breaks consecutive shake frames", "[screensaver]")
{
    ShakeDetector detector;
    TEST_ASSERT_FALSE(detector.update(2.0F, 0.0F, 0.0F, 0));
    TEST_ASSERT_FALSE(detector.update(0.0F, 0.0F, 1.0F, 50));
    TEST_ASSERT_FALSE(detector.update(2.0F, 0.0F, 0.0F, 100));
    TEST_ASSERT_FALSE(detector.update(2.0F, 0.0F, 0.0F, 1000));
    TEST_ASSERT_FALSE(detector.update(2.0F, 0.0F, 0.0F, 1100));
}

TEST_CASE("shake wakes screensaver without touch suppression", "[screensaver]")
{
    ScreensaverController controller;
    controller.reset(0, Page::Claude);
    controller.update(300000, Page::Claude, false, false);
    controller.note_shake(300020);

    TEST_ASSERT_FALSE(controller.view(300020).active);
    TEST_ASSERT_FALSE(controller.view(300020).consume_touch);
    TEST_ASSERT_TRUE(controller.touch_allowed(300020));
}

TEST_CASE("tilt wake fires on gravity rotation with constant magnitude", "[screensaver]")
{
    TiltWakeDetector detector;
    TEST_ASSERT_FALSE(detector.update(0.0F, 0.0F, 1.0F));
    TEST_ASSERT_FALSE(detector.update(0.0F, 0.0F, 1.0F));
    TEST_ASSERT_FALSE(detector.update(0.0F, 1.0F, 0.0F));
    TEST_ASSERT_TRUE(detector.update(0.0F, 1.0F, 0.0F));
}

TEST_CASE("zero-mean desk vibration does not tilt-wake", "[screensaver]")
{
    TiltWakeDetector detector;
    TEST_ASSERT_FALSE(detector.update(0.0F, 0.0F, 1.0F));
    const float wiggle[4][3] = {
        {0.02F, 0.0F, 1.0F},
        {-0.02F, 0.0F, 1.0F},
        {0.0F, 0.02F, 1.0F},
        {0.0F, -0.02F, 1.0F},
    };
    for (int i = 0; i < 20; ++i) {
        TEST_ASSERT_FALSE(detector.update(
            wiggle[i % 4][0], wiggle[i % 4][1], wiggle[i % 4][2]));
    }
}

TEST_CASE("small sustained tilt below threshold does not wake", "[screensaver]")
{
    TiltWakeDetector detector;
    TEST_ASSERT_FALSE(detector.update(0.0F, 0.0F, 1.0F));
    for (int i = 0; i < 10; ++i) {
        TEST_ASSERT_FALSE(detector.update(0.17365F, 0.0F, 0.98481F));
    }
}

TEST_CASE("shake detector wakes with 0.3 g excursion", "[screensaver]")
{
    ShakeDetector detector;
    TEST_ASSERT_FALSE(detector.update(0.0F, 0.0F, 1.0F, 0));
    TEST_ASSERT_FALSE(detector.update(1.3F, 0.0F, 0.0F, 100));
    TEST_ASSERT_FALSE(detector.update(1.3F, 0.0F, 0.0F, 200));
    TEST_ASSERT_FALSE(detector.update(1.3F, 0.0F, 0.0F, 300));
    TEST_ASSERT_TRUE(detector.update(1.3F, 0.0F, 0.0F, 400));
}

TEST_CASE("shake detector ignores 0.2 g excursion", "[screensaver]")
{
    ShakeDetector detector;
    for (int i = 0; i < 6; ++i) {
        TEST_ASSERT_FALSE(detector.update(
            1.2F, 0.0F, 0.0F, static_cast<uint64_t>(i * 100)));
    }
}

TEST_CASE("tilt wake is edge-triggered at the new pose", "[screensaver]")
{
    TiltWakeDetector detector;
    TEST_ASSERT_FALSE(detector.update(0.0F, 0.0F, 1.0F));
    TEST_ASSERT_FALSE(detector.update(0.0F, 0.0F, 1.0F));
    TEST_ASSERT_FALSE(detector.update(0.0F, 1.0F, 0.0F));
    TEST_ASSERT_TRUE(detector.update(0.0F, 1.0F, 0.0F));

    // Held still at the new orientation: the reference was snapped, so it must
    // not fire again until another reorientation.
    for (int i = 0; i < 10; ++i) {
        TEST_ASSERT_FALSE(detector.update(0.0F, 1.0F, 0.0F));
    }
}

TEST_CASE("remote toggle preserves touch wake and restarts idle timeout", "[screensaver]")
{
    ScreensaverController controller(1000);
    controller.reset(0, Page::Codex);
    controller.note_remote_toggle(100, Page::Claude);
    TEST_ASSERT_TRUE(controller.view(100).active);
    TEST_ASSERT_EQUAL(int(Page::Claude), int(controller.restore_page()));
    controller.note_touch_down(101);
    TEST_ASSERT_FALSE(controller.view(101).active);
    TEST_ASSERT_FALSE(controller.touch_allowed(400));
    TEST_ASSERT_TRUE(controller.touch_allowed(401));
    controller.update(1100, Page::Claude, false, false);
    TEST_ASSERT_FALSE(controller.view(1100).active);
    controller.update(1101, Page::Claude, false, false);
    TEST_ASSERT_TRUE(controller.view(1101).active);
    controller.note_remote_toggle(1200, Page::Claude);
    TEST_ASSERT_FALSE(controller.view(1200).active);
    controller.update(2199, Page::Claude, false, false);
    TEST_ASSERT_FALSE(controller.view(2199).active);
    controller.update(2200, Page::Claude, false, false);
    TEST_ASSERT_TRUE(controller.view(2200).active);
    controller.note_power_key(2201, Page::Claude);
    TEST_ASSERT_FALSE(controller.view(2201).active);
}

TEST_CASE("remote lock allows power wake and OTA keeps the screen awake", "[screensaver]")
{
    ScreensaverController controller;
    controller.reset(0, Page::Codex);
    controller.note_remote_toggle(100, Page::Codex);
    controller.note_power_key(101, Page::Codex);
    TEST_ASSERT_FALSE(controller.view(101).active);
    controller.note_remote_toggle(200, Page::Codex);
    controller.update(201, Page::Codex, false, true);
    TEST_ASSERT_FALSE(controller.view(201).active);
}

TEST_CASE("disabled automatic clock still permits explicit toggle", "[screensaver]")
{
    ScreensaverController controller(0);
    controller.reset(0, Page::Overview);
    controller.update(3600000, Page::Overview, false, false);
    TEST_ASSERT_FALSE(controller.view(3600000).active);
    controller.note_remote_toggle(3600000, Page::Overview);
    TEST_ASSERT_TRUE(controller.view(3600000).active);
    controller.reset(3600000, Page::Overview);
    controller.set_idle_delay(60000);
    controller.update(3660000, Page::Overview, false, false);
    TEST_ASSERT_TRUE(controller.view(3660000).active);
    uint32_t seconds = 42;
    const SettingsAction actions[] = {SettingsAction::TimeoutOff, SettingsAction::Timeout1,
        SettingsAction::Timeout5, SettingsAction::Timeout10, SettingsAction::Timeout30};
    const uint32_t expected[] = {0, 60, 300, 600, 1800};
    for (size_t i = 0; i < 5; ++i) {
        TEST_ASSERT_TRUE(select_clock_timeout(actions[i], seconds));
        TEST_ASSERT_EQUAL_UINT32(expected[i], seconds);
    }
    TEST_ASSERT_FALSE(select_clock_timeout(SettingsAction::Save, seconds));
    TEST_ASSERT_EQUAL_UINT32(1800, seconds);
}
