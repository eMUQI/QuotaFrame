#pragma once

#include "usage_panel_state/panel_state.hpp"
#include "usage_panel_state/screensaver_state.hpp"

namespace usage_panel {

struct PanelPresentation {
    ClockView clock;
    BatteryView battery;
    ScreensaverView screensaver;

    bool operator==(const PanelPresentation& other) const
    {
        return clock == other.clock && battery == other.battery &&
               screensaver == other.screensaver;
    }
    bool operator!=(const PanelPresentation& other) const
    {
        return !(*this == other);
    }
};

}  // namespace usage_panel
