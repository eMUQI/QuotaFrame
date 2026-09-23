#include "orientation.hpp"

#include "unity.h"

using namespace usage_panel;
using namespace usage_panel::amoled;

TEST_CASE("dominant acceleration maps to four screen rotations", "[amoled_orientation]")
{
    const auto up = rotation_from_acceleration(0.0F, 1.0F);
    const auto right = rotation_from_acceleration(1.0F, 0.0F);
    const auto down = rotation_from_acceleration(0.0F, -1.0F);
    const auto left = rotation_from_acceleration(-1.0F, 0.0F);

    TEST_ASSERT_TRUE(up.has_value());
    TEST_ASSERT_TRUE(right.has_value());
    TEST_ASSERT_TRUE(down.has_value());
    TEST_ASSERT_TRUE(left.has_value());
    TEST_ASSERT_EQUAL_UINT8(uint8_t(ScreenRotation::Deg270), uint8_t(*up));
    TEST_ASSERT_EQUAL_UINT8(uint8_t(ScreenRotation::Deg0), uint8_t(*right));
    TEST_ASSERT_EQUAL_UINT8(uint8_t(ScreenRotation::Deg90), uint8_t(*down));
    TEST_ASSERT_EQUAL_UINT8(uint8_t(ScreenRotation::Deg180), uint8_t(*left));
}

TEST_CASE("ambiguous and weak acceleration is rejected", "[amoled_orientation]")
{
    TEST_ASSERT_FALSE(rotation_from_acceleration(0.1F, 0.1F).has_value());
    TEST_ASSERT_FALSE(rotation_from_acceleration(0.6F, 0.0F).has_value());
    TEST_ASSERT_FALSE(rotation_from_acceleration(0.8F, 0.75F).has_value());
}

TEST_CASE("orientation tracker requires consecutive stable samples", "[amoled_orientation]")
{
    OrientationTracker tracker(3);

    TEST_ASSERT_FALSE(tracker.observe(0.0F, 1.0F));
    TEST_ASSERT_EQUAL_UINT8(uint8_t(ScreenRotation::Deg0), uint8_t(tracker.rotation()));
    TEST_ASSERT_FALSE(tracker.observe(0.0F, 1.0F));
    TEST_ASSERT_TRUE(tracker.observe(0.0F, 1.0F));
    TEST_ASSERT_EQUAL_UINT8(uint8_t(ScreenRotation::Deg270), uint8_t(tracker.rotation()));
}

TEST_CASE("orientation tracker commits the first stable default rotation", "[amoled_orientation]")
{
    OrientationTracker tracker(3);

    TEST_ASSERT_FALSE(tracker.observe(1.0F, 0.0F));
    TEST_ASSERT_FALSE(tracker.observe(1.0F, 0.0F));
    TEST_ASSERT_TRUE(tracker.observe(1.0F, 0.0F));
    TEST_ASSERT_EQUAL_UINT8(uint8_t(ScreenRotation::Deg0), uint8_t(tracker.rotation()));
}

TEST_CASE("orientation tracker resets a candidate after invalid or different samples", "[amoled_orientation]")
{
    OrientationTracker tracker(3);

    TEST_ASSERT_FALSE(tracker.observe(-1.0F, 0.0F));
    TEST_ASSERT_FALSE(tracker.observe(0.0F, 0.0F));
    TEST_ASSERT_FALSE(tracker.observe(-1.0F, 0.0F));
    TEST_ASSERT_FALSE(tracker.observe(0.0F, -1.0F));
    TEST_ASSERT_FALSE(tracker.observe(0.0F, -1.0F));
    TEST_ASSERT_TRUE(tracker.observe(0.0F, -1.0F));
    TEST_ASSERT_EQUAL_UINT8(uint8_t(ScreenRotation::Deg90), uint8_t(tracker.rotation()));
}

TEST_CASE("touch mappings cover all four hardware rotations", "[amoled_orientation]")
{
    const auto deg0 = touch_mapping(ScreenRotation::Deg0);
    const auto deg90 = touch_mapping(ScreenRotation::Deg90);
    const auto deg180 = touch_mapping(ScreenRotation::Deg180);
    const auto deg270 = touch_mapping(ScreenRotation::Deg270);

    TEST_ASSERT_FALSE(deg0.swap_xy);
    TEST_ASSERT_FALSE(deg0.mirror_x);
    TEST_ASSERT_FALSE(deg0.mirror_y);

    TEST_ASSERT_TRUE(deg90.swap_xy);
    TEST_ASSERT_TRUE(deg90.mirror_x);
    TEST_ASSERT_FALSE(deg90.mirror_y);

    TEST_ASSERT_FALSE(deg180.swap_xy);
    TEST_ASSERT_TRUE(deg180.mirror_x);
    TEST_ASSERT_TRUE(deg180.mirror_y);

    TEST_ASSERT_TRUE(deg270.swap_xy);
    TEST_ASSERT_FALSE(deg270.mirror_x);
    TEST_ASSERT_TRUE(deg270.mirror_y);
}

TEST_CASE("page navigation stays horizontal after touch coordinates are rotated", "[amoled_orientation]")
{
    const auto next = navigation_swipe(TouchDirection::Left);
    const auto previous = navigation_swipe(TouchDirection::Right);

    TEST_ASSERT_TRUE(next.has_value());
    TEST_ASSERT_TRUE(previous.has_value());
    TEST_ASSERT_EQUAL_UINT8(uint8_t(SwipeDirection::Next), uint8_t(*next));
    TEST_ASSERT_EQUAL_UINT8(
        uint8_t(SwipeDirection::Previous), uint8_t(*previous));
    TEST_ASSERT_FALSE(navigation_swipe(TouchDirection::Up).has_value());
    TEST_ASSERT_FALSE(navigation_swipe(TouchDirection::Down).has_value());
}
