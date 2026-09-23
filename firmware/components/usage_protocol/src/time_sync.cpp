#include "usage_protocol/time_sync.hpp"

#include <cstdint>
#include <limits>

#include "usage_protocol/usage_command.hpp"

namespace usage_panel {
namespace {

bool get_decimal_i16(const esp_desktop_buddy_command_view_t* request,
                     const char* key,
                     int16_t& value)
{
    const char* text = nullptr;
    if (esp_desktop_buddy_command_view_get_string(request, key, &text) != ESP_OK ||
        text == nullptr || text[0] == '\0') {
        return false;
    }

    bool negative = false;
    const char* cursor = text;
    if (*cursor == '-') {
        negative = true;
        ++cursor;
    }
    if (*cursor == '\0' || (*cursor == '0' && cursor[1] != '\0') ||
        (negative && *cursor == '0')) {
        return false;
    }

    int32_t parsed = 0;
    for (; *cursor != '\0'; ++cursor) {
        if (*cursor < '0' || *cursor > '9') return false;
        parsed = parsed * 10 + (*cursor - '0');
        if (parsed > std::numeric_limits<int16_t>::max() + int32_t{1}) {
            return false;
        }
    }
    parsed = negative ? -parsed : parsed;
    if (parsed < std::numeric_limits<int16_t>::min() ||
        parsed > std::numeric_limits<int16_t>::max()) {
        return false;
    }
    value = static_cast<int16_t>(parsed);
    return true;
}

bool is_leap_year(uint16_t year)
{
    return (year % 4u == 0u && year % 100u != 0u) || year % 400u == 0u;
}

uint8_t days_in_month(uint16_t year, uint8_t month)
{
    static constexpr uint8_t kDays[] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31,
    };
    if (month < 1 || month > 12) return 0;
    if (month == 2 && is_leap_year(year)) return 29;
    return kDays[month - 1];
}

}  // namespace

bool is_valid_calendar(const LocalCalendarTime& calendar)
{
    return calendar.year >= 2024 && calendar.year <= 2099 &&
           calendar.month >= 1 && calendar.month <= 12 &&
           calendar.day >= 1 &&
           calendar.day <= days_in_month(calendar.year, calendar.month) &&
           calendar.weekday <= 6 && calendar.hour <= 23 &&
           calendar.minute <= 59 && calendar.second <= 59 &&
           calendar.utc_offset_minutes >= -840 &&
           calendar.utc_offset_minutes <= 840;
}

TimeSyncCommandParseResult parse_time_sync_command(
    const esp_desktop_buddy_command_view_t* request)
{
    TimeSyncCommandParseResult result{};
    uint32_t version = 0;
    uint32_t year = 0;
    uint32_t month = 0;
    uint32_t day = 0;
    uint32_t weekday = 0;
    uint32_t hour = 0;
    uint32_t minute = 0;
    uint32_t second = 0;

    if (get_decimal_u32(request, "seq", result.sequence) != FieldResult::Valid ||
        get_decimal_u32(request, "v", version) != FieldResult::Valid ||
        version != 1 ||
        get_decimal_u32(request, "year", year) != FieldResult::Valid ||
        get_decimal_u32(request, "month", month) != FieldResult::Valid ||
        get_decimal_u32(request, "day", day) != FieldResult::Valid ||
        get_decimal_u32(request, "weekday", weekday) != FieldResult::Valid ||
        get_decimal_u32(request, "hour", hour) != FieldResult::Valid ||
        get_decimal_u32(request, "minute", minute) != FieldResult::Valid ||
        get_decimal_u32(request, "second", second) != FieldResult::Valid ||
        !get_decimal_i16(
            request, "utc_offset_min", result.calendar.utc_offset_minutes) ||
        year > std::numeric_limits<uint16_t>::max() ||
        month > std::numeric_limits<uint8_t>::max() ||
        day > std::numeric_limits<uint8_t>::max() ||
        weekday > std::numeric_limits<uint8_t>::max() ||
        hour > std::numeric_limits<uint8_t>::max() ||
        minute > std::numeric_limits<uint8_t>::max() ||
        second > std::numeric_limits<uint8_t>::max()) {
        return result;
    }

    result.calendar.year = static_cast<uint16_t>(year);
    result.calendar.month = static_cast<uint8_t>(month);
    result.calendar.day = static_cast<uint8_t>(day);
    result.calendar.weekday = static_cast<uint8_t>(weekday);
    result.calendar.hour = static_cast<uint8_t>(hour);
    result.calendar.minute = static_cast<uint8_t>(minute);
    result.calendar.second = static_cast<uint8_t>(second);
    result.valid = is_valid_calendar(result.calendar);
    return result;
}

}  // namespace usage_panel
