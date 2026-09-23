#pragma once

#include <cstdint>

#include "usage_ota/session.hpp"

namespace usage_panel {

enum class Page : uint8_t { Overview, Codex, Claude };
enum class SwipeDirection : uint8_t { Previous, Next };
enum class TouchAction : uint8_t { Navigate, Confirm, Deny, Ignore };
enum class OtaTouchControl : uint8_t { None, Confirm, Deny };

const char* page_name(Page page);
Page page_from_tab(uint8_t tab_index, Page fallback);
Page page_after_swipe(Page page, SwipeDirection direction);
TouchAction route_touch(OtaPhase phase, OtaTouchControl control);

}  // namespace usage_panel
