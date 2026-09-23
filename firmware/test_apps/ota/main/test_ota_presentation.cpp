#include "unity.h"
#include "usage_ota/presentation.hpp"

using namespace usage_panel;

TEST_CASE("OTA low power failure tells the user how to proceed", "[ota_ui]")
{
    const OtaErrorPresentation message = ota_error_presentation(OtaError::LowPower);
    TEST_ASSERT_EQUAL_STRING("LOW BATTERY", message.title);
    TEST_ASSERT_EQUAL_STRING("Connect USB power", message.detail);
}

TEST_CASE("OTA denial is distinguished from an update failure", "[ota_ui]")
{
    const OtaErrorPresentation message = ota_error_presentation(OtaError::Denied);
    TEST_ASSERT_EQUAL_STRING("CANCELLED", message.title);
    TEST_ASSERT_EQUAL_STRING("Cancelled on device", message.detail);
}

TEST_CASE("retained OTA error survives a cleared live snapshot", "[ota_ui]")
{
    OtaSnapshot live{};
    live.error = OtaError::None;

    const OtaSnapshot visible = ota_snapshot_for_display(
        live, OtaError::LowPower, true);

    TEST_ASSERT_EQUAL_INT(int(OtaError::LowPower), int(visible.error));
    TEST_ASSERT_EQUAL_INT(int(OtaError::None), int(live.error));
}

TEST_CASE("OTA percentage floors and clamps", "[ota_ui]")
{
    TEST_ASSERT_EQUAL_UINT8(0, ota_progress_percent(0, 0));
    TEST_ASSERT_EQUAL_UINT8(33, ota_progress_percent(1, 3));
    TEST_ASSERT_EQUAL_UINT8(100, ota_progress_percent(4, 3));
}

TEST_CASE("OTA confirmation countdown clamps at zero", "[ota_ui]")
{
    TEST_ASSERT_EQUAL_UINT32(60, ota_seconds_remaining(60000, 0));
    TEST_ASSERT_EQUAL_UINT32(60, ota_seconds_remaining(60005, 0));
    TEST_ASSERT_EQUAL_UINT32(1, ota_seconds_remaining(60000, 59999));
    TEST_ASSERT_EQUAL_UINT32(0, ota_seconds_remaining(60000, 60000));
    TEST_ASSERT_EQUAL_UINT32(0, ota_seconds_remaining(60000, 70000));
}

TEST_CASE("OTA text fitting keeps short strings unchanged", "[ota_ui]")
{
    TEST_ASSERT_EQUAL_STRING("v0.4.0", ota_fit_text("v0.4.0", 11).c_str());
}

TEST_CASE("OTA text fitting keeps string exactly at the limit", "[ota_ui]")
{
    TEST_ASSERT_EQUAL_STRING(
        "v0.4.0-32-g1a2b3c4d",
        ota_fit_text("v0.4.0-32-g1a2b3c4d", 19).c_str());
}

TEST_CASE("OTA text fitting truncates overlong strings with ellipsis", "[ota_ui]")
{
    const std::string fitted =
        ota_fit_text("v0.4.0-32-g1a2b3c4d-dirty", 11);
    TEST_ASSERT_EQUAL_UINT32(11, fitted.size());
    TEST_ASSERT_EQUAL_STRING("v0.4.0-32..", fitted.c_str());
}

TEST_CASE("OTA text fitting tolerates null and tiny limits", "[ota_ui]")
{
    TEST_ASSERT_EQUAL_STRING("", ota_fit_text(nullptr, 11).c_str());
    TEST_ASSERT_EQUAL_STRING("", ota_fit_text("v1", 0).c_str());
    TEST_ASSERT_EQUAL_STRING("v", ota_fit_text("v1", 1).c_str());
}
TEST_CASE("OTA failure presentation expires after three seconds", "[amoled_ota_ui]")
{
    TEST_ASSERT_FALSE(ota_failure_visible(OtaError::None, 1000, 1000));
    TEST_ASSERT_TRUE(ota_failure_visible(OtaError::Denied, 1000, 3999));
    TEST_ASSERT_FALSE(ota_failure_visible(OtaError::Denied, 1000, 4000));
}

TEST_CASE("each session failure starts its own visibility window", "[ota_presentation]")
{
    OtaSnapshot live{};
    RetainedOtaFailure shown{};

    live.error = OtaError::LowPower;
    live.failure_count = 1;
    TEST_ASSERT_TRUE(retain_ota_failure(live, shown, 1000));
    TEST_ASSERT_EQUAL(int(OtaError::LowPower), int(shown.error));
    TEST_ASSERT_EQUAL_UINT64(1000, shown.started_ms);

    // The same failure still being reported is not a new one.
    TEST_ASSERT_FALSE(retain_ota_failure(live, shown, 2000));
    TEST_ASSERT_EQUAL_UINT64(1000, shown.started_ms);

    // An immediately rejected retry keeps the same error and never reports
    // None in between, so only the counter tells the two failures apart.
    live.failure_count = 2;
    TEST_ASSERT_TRUE(retain_ota_failure(live, shown, 9000));
    TEST_ASSERT_EQUAL(int(OtaError::LowPower), int(shown.error));
    TEST_ASSERT_EQUAL_UINT64(9000, shown.started_ms);
    TEST_ASSERT_TRUE(ota_failure_visible(shown.error, shown.started_ms, 9500));

    // A successful attempt reports no error and starts no window.
    live.error = OtaError::None;
    live.failure_count = 2;
    TEST_ASSERT_FALSE(retain_ota_failure(live, shown, 12000));
}
