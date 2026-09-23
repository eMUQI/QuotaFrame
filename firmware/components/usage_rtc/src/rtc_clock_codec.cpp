#include "usage_rtc/rtc_clock.hpp"

namespace usage_panel::rtc_detail {
namespace {

bool decode_bcd(uint8_t value, uint8_t& out)
{
    const uint8_t tens = value >> 4;
    const uint8_t ones = value & 0x0F;
    if (tens > 9 || ones > 9) return false;
    out = static_cast<uint8_t>(tens * 10 + ones);
    return true;
}

uint8_t encode_bcd(uint8_t value)
{
    return static_cast<uint8_t>(((value / 10) << 4) | (value % 10));
}

}  // namespace

bool decode_calendar(
    const uint8_t registers[7], LocalCalendarTime& out)
{
    if (!registers || (registers[0] & 0x80) != 0) return false;

    LocalCalendarTime decoded{};
    uint8_t year = 0;
    if (!decode_bcd(registers[0] & 0x7F, decoded.second) ||
        !decode_bcd(registers[1] & 0x7F, decoded.minute) ||
        !decode_bcd(registers[2] & 0x3F, decoded.hour) ||
        !decode_bcd(registers[3] & 0x3F, decoded.day) ||
        !decode_bcd(registers[4] & 0x07, decoded.weekday) ||
        !decode_bcd(registers[5] & 0x1F, decoded.month) ||
        !decode_bcd(registers[6], year)) {
        return false;
    }
    decoded.year = static_cast<uint16_t>(2000 + year);
    if (!is_valid_calendar(decoded)) return false;
    out = decoded;
    return true;
}

bool encode_calendar(
    const LocalCalendarTime& calendar, uint8_t registers[7])
{
    if (!registers || !is_valid_calendar(calendar)) return false;
    registers[0] = encode_bcd(calendar.second);
    registers[1] = encode_bcd(calendar.minute);
    registers[2] = encode_bcd(calendar.hour);
    registers[3] = encode_bcd(calendar.day);
    registers[4] = encode_bcd(calendar.weekday);
    registers[5] = encode_bcd(calendar.month);
    registers[6] = encode_bcd(static_cast<uint8_t>(calendar.year - 2000));
    return true;
}

}  // namespace usage_panel::rtc_detail
