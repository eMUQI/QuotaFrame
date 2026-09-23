#include "usage_panel_state/page_state.hpp"

namespace usage_panel {

TouchAction route_touch(OtaPhase phase, OtaTouchControl control)
{
    if (phase == OtaPhase::Idle) return TouchAction::Navigate;
    if (phase == OtaPhase::Confirming) {
        if (control == OtaTouchControl::Confirm) return TouchAction::Confirm;
        if (control == OtaTouchControl::Deny) return TouchAction::Deny;
    }
    return TouchAction::Ignore;
}

const char* page_name(Page page)
{
    switch (page) {
    case Page::Codex: return "codex";
    case Page::Claude: return "claude";
    default: return "overview";
    }
}

Page page_from_tab(uint8_t tab_index, Page fallback)
{
    switch (tab_index) {
    case 0: return Page::Overview;
    case 1: return Page::Codex;
    case 2: return Page::Claude;
    default: return fallback;
    }
}

Page page_after_swipe(Page page, SwipeDirection direction)
{
    if (direction == SwipeDirection::Previous) {
        switch (page) {
        case Page::Overview: return Page::Claude;
        case Page::Codex: return Page::Overview;
        case Page::Claude: return Page::Codex;
        default: return Page::Overview;
        }
    }

    switch (page) {
    case Page::Overview: return Page::Codex;
    case Page::Codex: return Page::Claude;
    default: return Page::Overview;
    }
}

}  // namespace usage_panel
