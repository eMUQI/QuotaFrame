#include "button_controller.hpp"
#include "unity.h"
#include "usage_ota/session.hpp"

using namespace usage_panel;

TEST_CASE("M5 front button confirms only in confirming phase", "[ota_ui]")
{
    TEST_ASSERT_EQUAL(int(FrontButtonAction::Navigate),
                      int(route_front_button(OtaPhase::Idle)));
    TEST_ASSERT_EQUAL(int(FrontButtonAction::Confirm),
                      int(route_front_button(OtaPhase::Confirming)));
    TEST_ASSERT_EQUAL(int(FrontButtonAction::Ignore),
                      int(route_front_button(OtaPhase::Receiving)));
    TEST_ASSERT_EQUAL(int(FrontButtonAction::Ignore),
                      int(route_front_button(OtaPhase::Verifying)));
    TEST_ASSERT_EQUAL(int(FrontButtonAction::Ignore),
                      int(route_front_button(OtaPhase::Rebooting)));
}

TEST_CASE("M5 side button denies only in confirming phase", "[ota_ui]")
{
    TEST_ASSERT_EQUAL(int(SideButtonAction::Ignore),
                      int(route_side_button(OtaPhase::Idle)));
    TEST_ASSERT_EQUAL(int(SideButtonAction::Deny),
                      int(route_side_button(OtaPhase::Confirming)));
    TEST_ASSERT_EQUAL(int(SideButtonAction::Ignore),
                      int(route_side_button(OtaPhase::Receiving)));
}
