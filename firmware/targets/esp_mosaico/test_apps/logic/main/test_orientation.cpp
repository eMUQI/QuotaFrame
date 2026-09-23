#include "orientation.hpp"

#include "unity.h"

using namespace usage_panel;
using namespace usage_panel::mosaico;

TEST_CASE("gravity on a panel axis selects the upright rotation", "[mosaico_orientation]")
{
    // The axis reading +1 g points away from the earth, so the screen-down
    // axis (+Y) reading -1 g is the board held upright. The IMU is mounted
    // rotated 180 degrees from the panel, so upright still needs Deg180.
    const auto upright = rotation_from_acceleration(0.0F, -1.0F);
    const auto tilted_left = rotation_from_acceleration(-1.0F, 0.0F);
    const auto upside_down = rotation_from_acceleration(0.0F, 1.0F);
    const auto tilted_right = rotation_from_acceleration(1.0F, 0.0F);

    TEST_ASSERT_TRUE(upright.has_value());
    TEST_ASSERT_TRUE(tilted_left.has_value());
    TEST_ASSERT_TRUE(upside_down.has_value());
    TEST_ASSERT_TRUE(tilted_right.has_value());
    TEST_ASSERT_EQUAL_UINT8(uint8_t(ScreenRotation::Deg180), uint8_t(*upright));
    TEST_ASSERT_EQUAL_UINT8(uint8_t(ScreenRotation::Deg270), uint8_t(*tilted_left));
    TEST_ASSERT_EQUAL_UINT8(uint8_t(ScreenRotation::Deg0), uint8_t(*upside_down));
    TEST_ASSERT_EQUAL_UINT8(uint8_t(ScreenRotation::Deg90), uint8_t(*tilted_right));
}

TEST_CASE("ambiguous and weak acceleration is rejected", "[mosaico_orientation]")
{
    TEST_ASSERT_FALSE(rotation_from_acceleration(0.1F, 0.1F).has_value());
    TEST_ASSERT_FALSE(rotation_from_acceleration(0.6F, 0.0F).has_value());
    TEST_ASSERT_FALSE(rotation_from_acceleration(0.8F, 0.75F).has_value());
}
