#include "usage_rtc/rtc_clock.hpp"
#include "unity.h"

using namespace usage_panel;

TEST_CASE("RTC register decoder accepts valid 2000-based BCD", "[rtc_clock]")
{
    const uint8_t registers[7] = {
        0x59, 0x32, 0x14, 0x12, 0x03, 0x08, 0x26,
    };
    LocalCalendarTime calendar{};

    TEST_ASSERT_TRUE(rtc_detail::decode_calendar(registers, calendar));
    TEST_ASSERT_EQUAL_UINT16(2026, calendar.year);
    TEST_ASSERT_EQUAL_UINT8(8, calendar.month);
    TEST_ASSERT_EQUAL_UINT8(12, calendar.day);
    TEST_ASSERT_EQUAL_UINT8(3, calendar.weekday);
    TEST_ASSERT_EQUAL_UINT8(14, calendar.hour);
    TEST_ASSERT_EQUAL_UINT8(32, calendar.minute);
    TEST_ASSERT_EQUAL_UINT8(59, calendar.second);
}

TEST_CASE("RTC register decoder rejects oscillator stop and invalid BCD", "[rtc_clock]")
{
    LocalCalendarTime calendar{};
    const uint8_t stopped[7] = {
        0x80, 0x00, 0x00, 0x01, 0x00, 0x01, 0x26,
    };
    const uint8_t invalid_bcd[7] = {
        0x00, 0x6A, 0x00, 0x01, 0x00, 0x01, 0x26,
    };

    TEST_ASSERT_FALSE(rtc_detail::decode_calendar(stopped, calendar));
    TEST_ASSERT_FALSE(rtc_detail::decode_calendar(invalid_bcd, calendar));
}

TEST_CASE("RTC register encoder writes seven BCD calendar bytes", "[rtc_clock]")
{
    const LocalCalendarTime calendar{
        .year = 2026,
        .month = 8,
        .day = 12,
        .weekday = 3,
        .hour = 14,
        .minute = 32,
        .second = 59,
        .utc_offset_minutes = 480,
    };
    uint8_t registers[7]{};

    TEST_ASSERT_TRUE(rtc_detail::encode_calendar(calendar, registers));
    const uint8_t expected[7] = {
        0x59, 0x32, 0x14, 0x12, 0x03, 0x08, 0x26,
    };
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, registers, 7);

    LocalCalendarTime invalid = calendar;
    invalid.day = 31;
    invalid.month = 2;
    TEST_ASSERT_FALSE(rtc_detail::encode_calendar(invalid, registers));
}
