#include "usage_panel_state/usage_bar_animation.hpp"

namespace usage_panel {
namespace {

void set_usage_bar_value(void* target, int32_t value)
{
    lv_bar_set_value(static_cast<lv_obj_t*>(target), value, LV_ANIM_OFF);
}

}  // namespace

void configure_usage_bar_animation(lv_anim_t* animation)
{
    lv_anim_set_duration(animation, UI_ANIM_MS);
    lv_anim_set_path_cb(animation, lv_anim_path_ease_out);
}

void set_usage_bar_value_immediately(lv_obj_t* bar, int32_t value)
{
    lv_anim_delete(bar, set_usage_bar_value);
    lv_bar_set_value(bar, value, LV_ANIM_OFF);
}

void animate_usage_bar(lv_obj_t* bar, int32_t value)
{
    lv_anim_delete(bar, set_usage_bar_value);
    const int32_t start = lv_bar_get_value(bar);
    if (start == value) {
        return;
    }

    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, bar);
    lv_anim_set_exec_cb(&animation, set_usage_bar_value);
    lv_anim_set_values(&animation, start, value);
    configure_usage_bar_animation(&animation);
    lv_anim_start(&animation);
}

}  // namespace usage_panel
