#include "gesture_tracker.hpp"
#include "gesture_image.hpp"
#include "unity.h"

#include <array>
#include <limits>

using namespace usage_panel::mosaico;

namespace {
HandObservation hand(float x, float y = 0.5F, float ok = 0)
{
    return {1, x, y, 0.25F, 0.35F, 0.9F, ok};
}
void arm(GestureTracker& tracker)
{
    tracker.update({}, 1000);
    tracker.update({}, 1400);
}
}

TEST_CASE("swipes require release and issue exactly one directional command", "[gesture]")
{
    for (int direction : {-1, 1}) {
        GestureTracker tracker;
        arm(tracker);
        TEST_ASSERT_EQUAL_INT(int(GestureAction::None), int(tracker.update(hand(0.5F), 1500)));
        TEST_ASSERT_EQUAL_INT(int(GestureAction::None), int(tracker.update(hand(0.5F + direction * 0.15F), 1600)));
        const auto expected = direction < 0 ? GestureAction::SwipeLeft : GestureAction::SwipeRight;
        TEST_ASSERT_EQUAL_INT(int(expected), int(tracker.update(hand(0.5F + direction * 0.3F), 1700)));
        for (unsigned t = 1800; t < 3000; t += 100)
            TEST_ASSERT_EQUAL_INT(int(GestureAction::None), int(tracker.update(hand(0.5F), t)));
        tracker.update({}, 3000);
        tracker.update(hand(0.5F), 3100);
        TEST_ASSERT_EQUAL_INT(int(GestureAction::None), int(tracker.update(hand(0.8F), 3300)));
        tracker.update({}, 3400);
        tracker.update({}, 3800);
        tracker.update(hand(0.3F), 3900);
        tracker.update(hand(0.45F), 4000);
        TEST_ASSERT_EQUAL_INT(int(GestureAction::SwipeRight), int(tracker.update(hand(0.6F), 4100)));
    }
}

TEST_CASE("OK requires a stable hold and release after context invalidation", "[gesture]")
{
    GestureTracker tracker;
    arm(tracker);
    for (unsigned t = 1500; t < 2000; t += 100)
        TEST_ASSERT_EQUAL_INT(int(GestureAction::None), int(tracker.update(hand(0.5F, 0.5F, 0.95F), t)));
    TEST_ASSERT_EQUAL_INT(80, tracker.hold_progress());
    TEST_ASSERT_EQUAL_INT(int(GestureAction::EnterClock), int(tracker.update(hand(0.5F, 0.5F, 0.95F), 2000)));
    tracker.reset();
    for (unsigned t = 2100; t <= 3000; t += 100)
        TEST_ASSERT_EQUAL_INT(int(GestureAction::None), int(tracker.update(hand(0.5F, 0.5F, 0.95F), t)));
}

TEST_CASE("vertical motion jitter ambiguous hands and stale samples do not trigger", "[gesture]")
{
    for (unsigned scenario = 0; scenario < 5; ++scenario) {
        GestureTracker tracker;
        arm(tracker);
        for (unsigned i = 0; i < 7; ++i) {
            HandObservation observation = hand(0.2F + i * 0.08F);
            unsigned time = 1500 + i * 100;
            if (scenario == 0) { observation.x = 0.5F; observation.y = 0.2F + i * 0.1F; }
            if (scenario == 1) observation.x = i % 2 ? 0.55F : 0.45F;
            if (scenario == 2) observation.count = 2;
            if (scenario == 3) time = 1500 + i * 400;
            if (scenario == 4) observation.x = std::numeric_limits<float>::quiet_NaN();
            TEST_ASSERT_EQUAL_INT(int(GestureAction::None), int(tracker.update(observation, time)));
        }
    }
}

TEST_CASE("wake consumes the first swipe and protected states reject all commands", "[gesture]")
{
    TEST_ASSERT_EQUAL_INT(int(GestureEffect::Wake), int(route_gesture(GestureAction::SwipeLeft, true, true)));
    TEST_ASSERT_EQUAL_INT(int(GestureEffect::Wake), int(route_gesture(GestureAction::SwipeRight, true, true)));
    TEST_ASSERT_EQUAL_INT(int(GestureEffect::Next), int(route_gesture(GestureAction::SwipeLeft, true, false)));
    TEST_ASSERT_EQUAL_INT(int(GestureEffect::Previous), int(route_gesture(GestureAction::SwipeRight, true, false)));
    TEST_ASSERT_EQUAL_INT(int(GestureEffect::Clock), int(route_gesture(GestureAction::EnterClock, true, false)));
    TEST_ASSERT_EQUAL_INT(int(GestureEffect::None), int(route_gesture(GestureAction::EnterClock, true, true)));
    for (auto action : {GestureAction::SwipeLeft, GestureAction::SwipeRight, GestureAction::EnterClock})
        TEST_ASSERT_EQUAL_INT(int(GestureEffect::None), int(route_gesture(action, false, false)));
}

TEST_CASE("UYVY conversion respects padded stride mirror and four rotations", "[gesture]")
{
    // Four grayscale quadrants plus row padding make the geometric mapping observable.
    const uint8_t source[] = {128, 16, 128, 80, 255, 255, 128, 160, 128, 235, 255, 255};
    static std::array<uint8_t, kGestureImageBytes> output;
    const unsigned expected[4][4] = {{0, 74, 167, 255}, {167, 0, 255, 74},
                                    {255, 167, 74, 0}, {74, 255, 0, 167}};
    for (unsigned rotation = 0; rotation < 4; ++rotation) {
        const unsigned w = rotation & 1 ? kGestureImageHeight : kGestureImageWidth;
        const unsigned h = rotation & 1 ? kGestureImageWidth : kGestureImageHeight;
        TEST_ASSERT_TRUE(prepare_gesture_image(source, sizeof(source), 2, 2, 6, output.data(), rotation, false));
        const unsigned pixels[] = {0, w - 1, (h - 1) * w, h * w - 1};
        for (unsigned i = 0; i < 4; ++i)
            TEST_ASSERT_INT_WITHIN(1, expected[rotation][i], output[pixels[i] * 3]);
    }
    TEST_ASSERT_TRUE(prepare_gesture_image(source, sizeof(source), 2, 2, 6, output.data(), 0, true));
    TEST_ASSERT_INT_WITHIN(1, 74, output[0]);
    TEST_ASSERT_FALSE(prepare_gesture_image(source, 4, 2, 2, 6, output.data(), 0, false));
    TEST_ASSERT_FALSE(prepare_gesture_image(source, sizeof(source), 3, 2, 6, output.data(), 0, false));
}
