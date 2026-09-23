#pragma once

#include <cstdint>

#include "esp_desktop_buddy/esp_desktop_buddy.h"

namespace usage_panel {

/** Host-local calendar fields used to set the panel RTC without carrying identity data. */
struct LocalCalendarTime {
    uint16_t year = 0;
    uint8_t month = 0;
    uint8_t day = 0;
    uint8_t weekday = 0;  // 0 = Sunday, 6 = Saturday.
    uint8_t hour = 0;
    uint8_t minute = 0;
    uint8_t second = 0;
    int16_t utc_offset_minutes = 0;  // Local offset from UTC, including DST.
};

/** Result of decoding one time_sync version 1 command. */
struct TimeSyncCommandParseResult {
    bool valid = false;
    uint32_t sequence = 0;
    LocalCalendarTime calendar{};
};

/**
 * Validates the calendar range accepted by the wire protocol and RTC layer.
 * Years are intentionally limited to 2024-2099 and UTC offsets to +/-14 hours.
 */
bool is_valid_calendar(const LocalCalendarTime& calendar);

/**
 * Parses a time_sync version 1 command using canonical decimal fields.
 *
 * The result remains invalid for missing fields, leading-zero integer forms,
 * unsupported versions, impossible calendar values, or out-of-range offsets.
 */
TimeSyncCommandParseResult parse_time_sync_command(
    const esp_desktop_buddy_command_view_t* request);

}  // namespace usage_panel
