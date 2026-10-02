#include "gesture_tracker.hpp"
#include "gesture_image.hpp"
#include "unity.h"

#include <array>
#include <limits>

using namespace usage_panel::mosaico;

namespace {
std::array<uint8_t, kGestureImageBytes> image_output;

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

TEST_CASE("swipes repeat without release and ignore the same stroke and the return stroke", "[gesture]")
{
    for (bool vertical : {false, true}) {
        for (int direction : {-1, 1}) {
            // Positions are given for the positive direction and mirrored for the negative one.
            const auto point = [vertical, direction](float position) {
                const float mapped = 0.5F + direction * (position - 0.5F);
                return vertical ? hand(0.5F, mapped) : hand(mapped);
            };
            const auto expected = vertical
                ? (direction < 0 ? GestureAction::SwipeUp : GestureAction::SwipeDown)
                : (direction < 0 ? GestureAction::SwipeLeft : GestureAction::SwipeRight);
            const auto reverse = vertical
                ? (direction < 0 ? GestureAction::SwipeDown : GestureAction::SwipeUp)
                : (direction < 0 ? GestureAction::SwipeRight : GestureAction::SwipeLeft);
            const auto none = int(GestureAction::None);
            GestureTracker tracker;
            arm(tracker);
            TEST_ASSERT_EQUAL_INT(none, int(tracker.update(point(0.2F), 1500)));
            TEST_ASSERT_EQUAL_INT(none, int(tracker.update(point(0.35F), 1600)));
            TEST_ASSERT_EQUAL_INT(int(expected), int(tracker.update(point(0.5F), 1700)));
            tracker.reset(1800);  // The resulting page change keeps swipes armed.
            // The rest of the same stroke falls inside the cooldown.
            TEST_ASSERT_EQUAL_INT(none, int(tracker.update(point(0.65F), 1800)));
            TEST_ASSERT_EQUAL_INT(none, int(tracker.update(point(0.8F), 1900)));
            TEST_ASSERT_EQUAL_INT(none, int(tracker.update(point(0.95F), 2000)));
            // The hand returns through the view without leaving it.
            TEST_ASSERT_EQUAL_INT(none, int(tracker.update(point(0.8F), 2300)));
            TEST_ASSERT_EQUAL_INT(none, int(tracker.update(point(0.6F), 2400)));
            TEST_ASSERT_EQUAL_INT(none, int(tracker.update(point(0.4F), 2500)));
            TEST_ASSERT_EQUAL_INT(none, int(tracker.update(point(0.4F), 2600)));
            TEST_ASSERT_EQUAL_INT(none, int(tracker.update(point(0.55F), 2700)));
            TEST_ASSERT_EQUAL_INT(int(expected), int(tracker.update(point(0.7F), 2800)));
            // A stationary hand issues nothing; the reverse direction works once the return window ends.
            for (unsigned t = 2900; t < 4500; t += 100)
                TEST_ASSERT_EQUAL_INT(none, int(tracker.update(point(0.7F), t)));
            TEST_ASSERT_EQUAL_INT(none, int(tracker.update(point(0.55F), 4500)));
            TEST_ASSERT_EQUAL_INT(int(reverse), int(tracker.update(point(0.4F), 4600)));
            // A context change unrelated to a swipe requires the view to clear again.
            tracker.reset(6000);
            tracker.update(point(0.2F), 6100);
            tracker.update(point(0.35F), 6200);
            TEST_ASSERT_EQUAL_INT(none, int(tracker.update(point(0.5F), 6300)));
        }
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
    tracker.reset(2000);
    for (unsigned t = 2100; t <= 3000; t += 100)
        TEST_ASSERT_EQUAL_INT(int(GestureAction::None), int(tracker.update(hand(0.5F, 0.5F, 0.95F), t)));
}

TEST_CASE("single dropouts keep a swipe and an OK hold alive", "[gesture]")
{
    HandObservation weak = hand(0.6F);
    weak.score = 0.55F;
    for (const HandObservation& dropout : {HandObservation{}, weak}) {
        GestureTracker tracker;
        arm(tracker);
        tracker.update(hand(0.5F), 1500);
        tracker.update(dropout, 1600);
        tracker.update(hand(0.65F), 1700);
        TEST_ASSERT_EQUAL_INT(int(GestureAction::SwipeRight), int(tracker.update(hand(0.8F), 1800)));
    }

    GestureTracker tracker;
    arm(tracker);
    tracker.update(hand(0.5F, 0.5F, 0.95F), 1500);
    tracker.update(hand(0.5F, 0.5F, 0.95F), 1600);
    tracker.update(hand(0.5F, 0.5F, 0.95F), 1700);
    tracker.update({}, 1800);
    TEST_ASSERT_EQUAL_INT(40, tracker.hold_progress());
    tracker.update(hand(0.5F, 0.5F, 0.95F), 1900);
    TEST_ASSERT_EQUAL_INT(int(GestureAction::EnterClock), int(tracker.update(hand(0.5F, 0.5F, 0.95F), 2000)));
}

TEST_CASE("OK hold completes at a 300 ms frame interval", "[gesture]")
{
    GestureTracker tracker;
    arm(tracker);
    tracker.update(hand(0.5F, 0.5F, 0.95F), 1500);
    tracker.update(hand(0.5F, 0.5F, 0.95F), 1800);
    TEST_ASSERT_EQUAL_INT(int(GestureAction::EnterClock), int(tracker.update(hand(0.5F, 0.5F, 0.95F), 2100)));
}

TEST_CASE("slow frames and position jumps restart tracking without requiring release", "[gesture]")
{
    GestureTracker tracker;
    arm(tracker);
    tracker.update(hand(0.2F), 1500);
    tracker.update(hand(0.2F), 2000);  // 500 ms gap: restart here.
    tracker.update(hand(0.35F), 2100);
    TEST_ASSERT_EQUAL_INT(int(GestureAction::SwipeRight), int(tracker.update(hand(0.5F), 2200)));

    tracker.update({}, 2300);
    tracker.update({}, 2700);
    tracker.update(hand(0.9F), 2800);
    tracker.update(hand(0.3F), 2900);  // Jump: restart here.
    tracker.update(hand(0.45F), 3000);
    TEST_ASSERT_EQUAL_INT(int(GestureAction::SwipeRight), int(tracker.update(hand(0.6F), 3100)));
}

TEST_CASE("diagonal motion jitter ambiguous hands and stale samples do not trigger", "[gesture]")
{
    for (unsigned scenario = 0; scenario < 6; ++scenario) {
        GestureTracker tracker;
        arm(tracker);
        for (unsigned i = 0; i < 7; ++i) {
            HandObservation observation = hand(0.2F + i * 0.08F);
            unsigned time = 1500 + i * 100;
            if (scenario == 0) observation.y = observation.x;
            if (scenario == 1) observation.x = i % 2 ? 0.55F : 0.45F;
            if (scenario == 2) observation.count = 2;
            if (scenario == 3) time = 1500 + i * 400;
            if (scenario == 4) observation.x = std::numeric_limits<float>::quiet_NaN();
            if (scenario == 5) { observation.x = 0.5F; observation.y = i % 2 ? 0.55F : 0.45F; }
            TEST_ASSERT_EQUAL_INT(int(GestureAction::None), int(tracker.update(observation, time)));
        }
    }
}

TEST_CASE("wake consumes the first swipe and protected states reject all commands", "[gesture]")
{
    TEST_ASSERT_EQUAL_INT(int(GestureEffect::Wake), int(route_gesture(GestureAction::SwipeLeft, true, true)));
    TEST_ASSERT_EQUAL_INT(int(GestureEffect::Wake), int(route_gesture(GestureAction::SwipeRight, true, true)));
    TEST_ASSERT_EQUAL_INT(int(GestureEffect::Previous), int(route_gesture(GestureAction::SwipeLeft, true, false)));
    TEST_ASSERT_EQUAL_INT(int(GestureEffect::Next), int(route_gesture(GestureAction::SwipeRight, true, false)));
    TEST_ASSERT_EQUAL_INT(int(GestureEffect::Wake), int(route_gesture(GestureAction::SwipeUp, true, true)));
    TEST_ASSERT_EQUAL_INT(int(GestureEffect::Wake), int(route_gesture(GestureAction::SwipeDown, true, true)));
    TEST_ASSERT_EQUAL_INT(int(GestureEffect::Previous), int(route_gesture(GestureAction::SwipeUp, true, false)));
    TEST_ASSERT_EQUAL_INT(int(GestureEffect::Next), int(route_gesture(GestureAction::SwipeDown, true, false)));
    TEST_ASSERT_EQUAL_INT(int(GestureEffect::Clock), int(route_gesture(GestureAction::EnterClock, true, false)));
    TEST_ASSERT_EQUAL_INT(int(GestureEffect::None), int(route_gesture(GestureAction::EnterClock, true, true)));
    for (auto action : {GestureAction::SwipeLeft, GestureAction::SwipeRight, GestureAction::SwipeUp,
                        GestureAction::SwipeDown, GestureAction::EnterClock})
        TEST_ASSERT_EQUAL_INT(int(GestureEffect::None), int(route_gesture(action, false, false)));
}

TEST_CASE("UYVY conversion respects padded stride mirror and four rotations", "[gesture]")
{
    // Four grayscale quadrants plus row padding make the geometric mapping observable.
    const uint8_t source[] = {128, 16, 128, 80, 255, 255, 128, 160, 128, 235, 255, 255};
    auto& output = image_output;
    const unsigned expected[4][4] = {{0, 74, 167, 255}, {167, 0, 255, 74},
                                    {255, 167, 74, 0}, {74, 255, 0, 167}};
    for (unsigned rotation = 0; rotation < 4; ++rotation) {
        const auto size = gesture_image_size(2, 2, rotation);
        const unsigned w = size.width;
        const unsigned h = size.height;
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

TEST_CASE("full sensor frame preserves aspect ratio and edges through rotation", "[gesture]")
{
    const auto hd = gesture_image_size(1280, 720, 0);
    TEST_ASSERT_EQUAL_UINT(320, hd.width);
    TEST_ASSERT_EQUAL_UINT(180, hd.height);
    const auto vga = gesture_image_size(640, 480, 0);
    TEST_ASSERT_EQUAL_UINT(320, vga.width);
    TEST_ASSERT_EQUAL_UINT(240, vga.height);
    TEST_ASSERT_EQUAL_UINT(0, gesture_image_size(0, 720, 0).width);
    TEST_ASSERT_EQUAL_UINT(0, gesture_image_size(1280, 720, 4).width);

    // A 16:9 frame with distinct corners verifies that fitting does not crop edges.
    constexpr unsigned stride = 36;
    std::array<uint8_t, stride * 9> source{};
    for (unsigned y = 0; y < 9; ++y) {
        for (unsigned x = 0; x < 16; x += 2) {
            const uint8_t luma = y < 4 ? (x < 8 ? 16 : 80) : (x < 8 ? 160 : 235);
            const unsigned offset = y * stride + x * 2;
            source[offset] = source[offset + 2] = 128;
            source[offset + 1] = source[offset + 3] = luma;
        }
    }
    auto& output = image_output;
    const unsigned corners[2][4][4] = {
        {{0, 74, 167, 255}, {167, 0, 255, 74}, {255, 167, 74, 0}, {74, 255, 0, 167}},
        {{74, 0, 255, 167}, {255, 74, 167, 0}, {167, 255, 0, 74}, {0, 167, 74, 255}}
    };
    for (unsigned mirror = 0; mirror < 2; ++mirror) {
        for (unsigned rotation = 0; rotation < 4; ++rotation) {
            output.fill(42);
            const auto size = gesture_image_size(16, 9, rotation);
            TEST_ASSERT_EQUAL_UINT(rotation & 1 ? 180 : 320, size.width);
            TEST_ASSERT_EQUAL_UINT(rotation & 1 ? 320 : 180, size.height);
            TEST_ASSERT_TRUE(prepare_gesture_image(source.data(), source.size(), 16, 9,
                                                   stride, output.data(), rotation, mirror));
            const unsigned pixels[] = {0, size.width - 1, (size.height - 1) * size.width,
                                       size.width * size.height - 1};
            for (unsigned i = 0; i < 4; ++i)
                TEST_ASSERT_INT_WITHIN(1, corners[mirror][rotation][i], output[pixels[i] * 3]);
            TEST_ASSERT_EQUAL_UINT8(42, output[size.width * size.height * 3]);
        }
    }
}
