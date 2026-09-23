#pragma once
#include <cstdint>

#include "page_state.hpp"
#include "usage_ota/session.hpp"

namespace usage_panel {

enum class FrontButtonAction : uint8_t { Navigate, Confirm, Ignore };
enum class SideButtonAction : uint8_t { Deny, Ignore };

inline Page handle_front_button(Page current) { return next_page(current); }
FrontButtonAction route_front_button(OtaPhase phase);
SideButtonAction route_side_button(OtaPhase phase);

}
