#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "usage_ota/session.hpp"

namespace usage_panel {

/** Returns transfer progress as an integer percentage, clamped to 0-100. */
uint8_t ota_progress_percent(uint32_t offset, uint32_t size);

/** Returns whole seconds remaining in the 60-second confirmation window,
 *  rounding a partial final second up. */
uint32_t ota_seconds_remaining(uint64_t deadline_ms, uint64_t now_ms);

/** Returns whether a non-None OTA error is still inside the UI visibility window. */
bool ota_failure_visible(
    OtaError error, uint64_t failure_started_ms, uint64_t now_ms);

/** The failure the panel is showing, retained across the session's own resets. */
struct RetainedOtaFailure {
    OtaError error = OtaError::None;
    uint64_t started_ms = 0;
    uint32_t count = 0;
};

/**
 * Starts a visibility window for each distinct session failure.
 *
 * The retained failure outlives the session's own state so the overlay
 * survives the reset that arms the next attempt. Distinctness comes from the
 * session's failure counter rather than the error value, because an immediately
 * rejected retry keeps the same OtaError without ever reporting None.
 *
 * @param[in] live The session's current snapshot.
 * @param[in,out] shown The failure the panel is presenting, updated in place.
 * @param[in] now_ms Monotonic milliseconds.
 *
 * @return true when a new failure started its window and the panel must redraw.
 */
bool retain_ota_failure(
    const OtaSnapshot& live, RetainedOtaFailure& shown, uint64_t now_ms);

/** Compact device-facing explanation for a retained OTA failure. */
struct OtaErrorPresentation {
    const char* title;
    const char* detail;
};

OtaSnapshot ota_snapshot_for_display(
    const OtaSnapshot& live, OtaError retained_error, bool show_retained);

OtaErrorPresentation ota_error_presentation(OtaError error);

/** Truncates text to max_chars characters, appending ".." when cut. */
std::string ota_fit_text(const char* text, size_t max_chars);

}  // namespace usage_panel
