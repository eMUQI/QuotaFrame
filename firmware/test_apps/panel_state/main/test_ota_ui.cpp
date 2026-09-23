#include "usage_panel_state/page_state.hpp"
#include "unity.h"

using namespace usage_panel;

TEST_CASE("panel touch confirms only on the OTA control", "[panel_ota_ui]")
{
    TEST_ASSERT_EQUAL(int(TouchAction::Navigate),
                      int(route_touch(OtaPhase::Idle, OtaTouchControl::None)));
    TEST_ASSERT_EQUAL(int(TouchAction::Confirm),
                      int(route_touch(OtaPhase::Confirming, OtaTouchControl::Confirm)));
    TEST_ASSERT_EQUAL(int(TouchAction::Deny),
                      int(route_touch(OtaPhase::Confirming, OtaTouchControl::Deny)));
    TEST_ASSERT_EQUAL(int(TouchAction::Ignore),
                      int(route_touch(OtaPhase::Confirming, OtaTouchControl::None)));
    TEST_ASSERT_EQUAL(int(TouchAction::Ignore),
                      int(route_touch(OtaPhase::Receiving, OtaTouchControl::Confirm)));
    TEST_ASSERT_EQUAL(int(TouchAction::Ignore),
                      int(route_touch(OtaPhase::Verifying, OtaTouchControl::Deny)));
    TEST_ASSERT_EQUAL(int(TouchAction::Ignore),
                      int(route_touch(OtaPhase::Rebooting, OtaTouchControl::Confirm)));
}
