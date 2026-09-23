#include "local_clock.hpp"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "unity.h"

using namespace usage_panel;
using namespace usage_panel::mosaico;

namespace {
LocalCalendarTime make_calendar()
{
    LocalCalendarTime calendar{};
    calendar.year = 2026;
    calendar.month = 9;
    calendar.day = 18;
    calendar.weekday = 5;
    calendar.hour = 14;
    calendar.minute = 32;
    calendar.second = 10;
    calendar.utc_offset_minutes = 480;
    return calendar;
}
}  // namespace

TEST_CASE("local clock reports nothing before the first sync", "[local_clock]")
{
    // Ordered first: the module tracks whether any calendar has arrived in
    // this power cycle, and a later sync makes that state unreachable.
    LocalCalendarTime out{};
    TEST_ASSERT_FALSE(read_local_calendar(out));
}

TEST_CASE("local clock reads back the calendar it was set to", "[local_clock]")
{
    const LocalCalendarTime calendar = make_calendar();
    set_local_calendar(calendar);

    LocalCalendarTime out{};
    TEST_ASSERT_TRUE(read_local_calendar(out));
    TEST_ASSERT_EQUAL_UINT16(calendar.year, out.year);
    TEST_ASSERT_EQUAL_UINT8(calendar.month, out.month);
    TEST_ASSERT_EQUAL_UINT8(calendar.day, out.day);
    TEST_ASSERT_EQUAL_UINT8(calendar.hour, out.hour);
    TEST_ASSERT_EQUAL_UINT8(calendar.minute, out.minute);
    TEST_ASSERT_EQUAL_INT16(calendar.utc_offset_minutes, out.utc_offset_minutes);
    // 2026-09-18 is a Friday; the weekday is derived, not stored.
    TEST_ASSERT_EQUAL_UINT8(5, out.weekday);
}

TEST_CASE("local clock rolls the calendar over midnight", "[local_clock]")
{
    LocalCalendarTime calendar = make_calendar();
    calendar.month = 12;
    calendar.day = 31;
    calendar.hour = 23;
    calendar.minute = 59;
    calendar.second = 59;
    set_local_calendar(calendar);

    vTaskDelay(pdMS_TO_TICKS(1500));

    LocalCalendarTime out{};
    TEST_ASSERT_TRUE(read_local_calendar(out));
    TEST_ASSERT_EQUAL_UINT16(2027, out.year);
    TEST_ASSERT_EQUAL_UINT8(1, out.month);
    TEST_ASSERT_EQUAL_UINT8(1, out.day);
    TEST_ASSERT_EQUAL_UINT8(0, out.hour);
    TEST_ASSERT_EQUAL_UINT8(0, out.minute);
}
