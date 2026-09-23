#include "unity.h"

#include "lvgl.h"
#include "usage_panel_state/usage_bar_animation.hpp"

using namespace usage_panel;

TEST_CASE("usage bar animation is 180ms ease out", "[display_ui]")
{
    lv_anim_t animation;
    lv_anim_init(&animation);
    configure_usage_bar_animation(&animation);

    TEST_ASSERT_EQUAL_UINT32(180, animation.duration);

    animation.start_value = 0;
    animation.end_value = 100;
    animation.act_time = animation.duration / 2;
    const int32_t midpoint = animation.path_cb(&animation);

    TEST_ASSERT_GREATER_THAN_INT32(50, midpoint);
    TEST_ASSERT_LESS_THAN_INT32(100, midpoint);
}
