#include "local_clock.hpp"

#include <ctime>
#include <sys/time.h>

namespace usage_panel::mosaico {
namespace {
bool s_calendar_received = false;
int16_t s_utc_offset_minutes = 0;
}  // namespace

void set_local_calendar(const LocalCalendarTime& calendar)
{
    std::tm fields{};
    fields.tm_year = calendar.year - 1900;
    fields.tm_mon = calendar.month - 1;
    fields.tm_mday = calendar.day;
    fields.tm_hour = calendar.hour;
    fields.tm_min = calendar.minute;
    fields.tm_sec = calendar.second;
    fields.tm_isdst = 0;

    const timeval now{.tv_sec = timegm(&fields), .tv_usec = 0};
    settimeofday(&now, nullptr);
    s_utc_offset_minutes = calendar.utc_offset_minutes;
    s_calendar_received = true;
}

bool read_local_calendar(LocalCalendarTime& out)
{
    if (!s_calendar_received) {
        return false;
    }
    const std::time_t now = std::time(nullptr);
    std::tm fields{};
    if (gmtime_r(&now, &fields) == nullptr) {
        return false;
    }
    out.year = static_cast<uint16_t>(fields.tm_year + 1900);
    out.month = static_cast<uint8_t>(fields.tm_mon + 1);
    out.day = static_cast<uint8_t>(fields.tm_mday);
    out.weekday = static_cast<uint8_t>(fields.tm_wday);
    out.hour = static_cast<uint8_t>(fields.tm_hour);
    out.minute = static_cast<uint8_t>(fields.tm_min);
    out.second = static_cast<uint8_t>(fields.tm_sec);
    out.utc_offset_minutes = s_utc_offset_minutes;
    return true;
}

}  // namespace usage_panel::mosaico
