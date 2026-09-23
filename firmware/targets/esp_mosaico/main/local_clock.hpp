#pragma once

#include "usage_protocol/time_sync.hpp"

namespace usage_panel::mosaico {

/**
 * Wall clock backed by the SoC system time.
 *
 * The board carries no RTC chip, so the calendar survives a reset only while
 * the RTC timer keeps running; it is lost on power loss and has to be sent
 * again by the bridge. The bridge supplies an already-local calendar, so the
 * value is stored as if it were UTC and read back the same way, keeping the
 * libc timezone out of the round trip.
 */
void set_local_calendar(const LocalCalendarTime& calendar);

/** Returns false until a calendar has been received in this power cycle. */
bool read_local_calendar(LocalCalendarTime& out);

}  // namespace usage_panel::mosaico
