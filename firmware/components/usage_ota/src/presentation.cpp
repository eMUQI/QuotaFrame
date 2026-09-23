#include "usage_ota/presentation.hpp"

#include <algorithm>

namespace usage_panel {

uint8_t ota_progress_percent(uint32_t offset, uint32_t size)
{
    if (size == 0) return 0;
    const uint64_t percent = uint64_t(offset) * 100u / size;
    return static_cast<uint8_t>(std::min<uint64_t>(percent, 100u));
}

uint32_t ota_seconds_remaining(uint64_t deadline_ms, uint64_t now_ms)
{
    if (now_ms >= deadline_ms) return 0;
    // The deadline is recorded on the BLE task and can be slightly newer than a
    // now_ms sampled earlier by the main loop.
    return std::min<uint32_t>(
        60, static_cast<uint32_t>((deadline_ms - now_ms + 999u) / 1000u));
}

bool ota_failure_visible(
    OtaError error, uint64_t failure_started_ms, uint64_t now_ms)
{
    return error != OtaError::None && now_ms >= failure_started_ms &&
           now_ms - failure_started_ms < 3000u;
}

bool retain_ota_failure(
    const OtaSnapshot& live, RetainedOtaFailure& shown, uint64_t now_ms)
{
    if (live.error == OtaError::None || live.failure_count == shown.count) {
        return false;
    }
    shown.error = live.error;
    shown.started_ms = now_ms;
    shown.count = live.failure_count;
    return true;
}

OtaSnapshot ota_snapshot_for_display(
    const OtaSnapshot& live, OtaError retained_error, bool show_retained)
{
    OtaSnapshot visible = live;
    if (show_retained && retained_error != OtaError::None) {
        visible.error = retained_error;
    }
    return visible;
}

OtaErrorPresentation ota_error_presentation(OtaError error)
{
    switch (error) {
    case OtaError::Denied:
        return {"CANCELLED", "Cancelled on device"};
    case OtaError::Timeout:
        return {"NO CONFIRM", "Try update again"};
    case OtaError::TooLarge:
        return {"TOO LARGE", "Firmware does not fit"};
    case OtaError::BadImage:
        return {"BAD IMAGE", "Firmware was rejected"};
    case OtaError::LinkLost:
        return {"LINK LOST", "Keep device nearby"};
    case OtaError::SeqGap:
        return {"DATA ERROR", "Try update again"};
    case OtaError::ProjectMismatch:
        return {"WRONG PROJECT", "Choose matching firmware"};
    case OtaError::TargetMismatch:
        return {"WRONG TARGET", "Choose matching firmware"};
    case OtaError::LowPower:
        return {"LOW BATTERY", "Connect USB power"};
    case OtaError::None:
    default:
        return {"UPDATE FAILED", "Current firmware active"};
    }
}

std::string ota_fit_text(const char* text, size_t max_chars)
{
    if (!text) return {};
    const std::string input(text);
    if (input.size() <= max_chars) return input;
    if (max_chars < 2) return input.substr(0, max_chars);
    return input.substr(0, max_chars - 2) + "..";
}

}  // namespace usage_panel
