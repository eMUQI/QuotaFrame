#pragma once

#include <cstdint>

#include "driver/i2c_master.h"
#include "usage_protocol/time_sync.hpp"

namespace usage_panel {

/**
 * PCF85063A local wall-clock adapter on a caller-owned I2C bus.
 *
 * The RTC stores calendar fields only; it has no timezone-offset register.
 * `utc_offset_minutes` is therefore validated when a Bridge calendar is
 * encoded but is not persisted, and calendars read back from the RTC carry the
 * default offset value. The panel UI consumes local wall-clock fields only.
 */
class RtcClock {
public:
    /**
     * Adds the RTC device at 0x51 and writes Control_1 with the 12.5 pF
     * crystal load selected, which also clears STOP and selects 24-hour mode.
     * Idempotent for the same bus.
     */
    bool begin(i2c_master_bus_handle_t bus);

    /** Reads and validates the seven calendar registers. */
    bool read(LocalCalendarTime& out);

    /** Stops the clock, writes a validated calendar, then resumes it. */
    bool write(const LocalCalendarTime& calendar);

private:
    i2c_master_bus_handle_t bus_ = nullptr;
    i2c_master_dev_handle_t device_ = nullptr;
};

namespace rtc_detail {

/** Decodes PCF85063A BCD calendar registers; the oscillator-stop flag is rejected. */
bool decode_calendar(
    const uint8_t registers[7], LocalCalendarTime& out);

/** Encodes validated local calendar fields into PCF85063A BCD registers. */
bool encode_calendar(
    const LocalCalendarTime& calendar, uint8_t registers[7]);

}  // namespace rtc_detail
}  // namespace usage_panel
