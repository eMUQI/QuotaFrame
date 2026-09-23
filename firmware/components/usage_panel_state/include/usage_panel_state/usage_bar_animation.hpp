#pragma once

#include <cstdint>

#include "lvgl.h"

namespace usage_panel {

// The panel can only afford motion inside narrow transfer strips. UI motion
// shares one duration so the navigation and usage bars read as one system.
constexpr uint32_t UI_ANIM_MS = 180;

/** Applies the shared duration and ease-out path to a usage-bar animation. */
void configure_usage_bar_animation(lv_anim_t* animation);

/** Animates a usage bar from its current value to the requested percentage. */
void animate_usage_bar(lv_obj_t* bar, int32_t value);

/** Cancels any usage animation and applies the value immediately. */
void set_usage_bar_value_immediately(lv_obj_t* bar, int32_t value);

}  // namespace usage_panel
