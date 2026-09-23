#include "button_controller.hpp"

namespace usage_panel {

FrontButtonAction route_front_button(OtaPhase phase)
{
    if (phase == OtaPhase::Idle) return FrontButtonAction::Navigate;
    if (phase == OtaPhase::Confirming) return FrontButtonAction::Confirm;
    return FrontButtonAction::Ignore;
}

SideButtonAction route_side_button(OtaPhase phase)
{
    return phase == OtaPhase::Confirming
        ? SideButtonAction::Deny : SideButtonAction::Ignore;
}

}  // namespace usage_panel
