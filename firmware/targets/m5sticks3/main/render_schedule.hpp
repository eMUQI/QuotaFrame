#pragma once

#include <cstdint>

#include "battery_view.hpp"
#include "page_state.hpp"
#include "usage_core/usage_state.hpp"

namespace usage_panel {

/**
 * Minimal state that can change a rendered page as monotonic time advances.
 * Keeping this separate from the full model lets the main loop poll once per
 * second without redrawing the display when neither status nor countdown text
 * changed.
 */
struct TimedRenderKey {
    Page page = Page::Overview;
    DisplayState first_state = DisplayState::NoData;
    DisplayState second_state = DisplayState::NoData;
    uint32_t ota_confirm_seconds = 0;
    char short_countdown[16]{};
    char week_countdown[16]{};
    usage_panel::m5::BatteryView battery{};

    bool operator==(const TimedRenderKey& other) const;
    bool operator!=(const TimedRenderKey& other) const { return !(*this == other); }
};

/** Builds the time-sensitive render key for the current page. */
TimedRenderKey make_timed_render_key(
    const UsageModel& model, Page page, bool connected, uint64_t now_ms,
    const usage_panel::m5::BatteryView& battery);

}  // namespace usage_panel
