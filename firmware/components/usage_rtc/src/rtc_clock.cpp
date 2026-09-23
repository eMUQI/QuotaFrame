#include "usage_rtc/rtc_clock.hpp"

namespace usage_panel {
namespace {

constexpr uint8_t kAddress = 0x51;
constexpr uint32_t kSclSpeedHz = 400000;
constexpr int kTimeoutMs = 1000;
constexpr uint8_t kControl1Register = 0x00;
constexpr uint8_t kSecondsRegister = 0x04;
constexpr uint8_t kStopBit = 0x20;
constexpr uint8_t kTwelveHourBit = 0x02;
constexpr uint8_t kCapacitorSelectBit = 0x01;

bool write_bytes(i2c_master_dev_handle_t device, const uint8_t* data, size_t size)
{
    return i2c_master_transmit(device, data, size, kTimeoutMs) == ESP_OK;
}

bool read_registers(
    i2c_master_dev_handle_t device, uint8_t reg, uint8_t* data, size_t size)
{
    return i2c_master_transmit_receive(device, &reg, 1, data, size, kTimeoutMs) ==
           ESP_OK;
}

}  // namespace

bool RtcClock::begin(i2c_master_bus_handle_t bus)
{
    if (!bus) return false;
    if (device_) return bus == bus_;

    i2c_device_config_t config{};
    config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    config.device_address = kAddress;
    config.scl_speed_hz = kSclSpeedHz;
    i2c_master_dev_handle_t device = nullptr;
    if (i2c_master_bus_add_device(bus, &config, &device) != ESP_OK) return false;

    const uint8_t control_command[] = {kControl1Register, kCapacitorSelectBit};
    if (!write_bytes(device, control_command, sizeof(control_command))) {
        i2c_master_bus_rm_device(device);
        return false;
    }
    bus_ = bus;
    device_ = device;
    return true;
}

bool RtcClock::read(LocalCalendarTime& out)
{
    if (!device_) return false;
    // Read the contiguous calendar block in one transaction so fields belong to
    // one RTC snapshot instead of crossing a second/minute rollover piecemeal.
    uint8_t registers[7]{};
    if (!read_registers(device_, kSecondsRegister, registers, sizeof(registers))) {
        return false;
    }
    return rtc_detail::decode_calendar(registers, out);
}

bool RtcClock::write(const LocalCalendarTime& calendar)
{
    uint8_t registers[7]{};
    if (!device_ || !rtc_detail::encode_calendar(calendar, registers)) {
        return false;
    }

    uint8_t control = 0;
    if (!read_registers(device_, kControl1Register, &control, 1)) {
        return false;
    }

    // Stop the oscillator while replacing all calendar registers. Otherwise
    // the RTC can advance between field writes and expose an internally
    // inconsistent time. Force 24-hour mode while preserving other control bits.
    const uint8_t stop_command[] = {
        kControl1Register,
        static_cast<uint8_t>((control | kStopBit) & ~kTwelveHourBit),
    };
    if (!write_bytes(device_, stop_command, sizeof(stop_command))) {
        return false;
    }

    uint8_t calendar_command[8]{kSecondsRegister};
    for (size_t i = 0; i < sizeof(registers); ++i) {
        calendar_command[i + 1] = registers[i];
    }
    const bool calendar_written =
        write_bytes(device_, calendar_command, sizeof(calendar_command));

    // Always attempt to restart the oscillator even if the calendar write
    // failed; leaving STOP asserted would turn a transient I2C error into a
    // persistent frozen-clock failure.
    const uint8_t resume_command[] = {
        kControl1Register,
        static_cast<uint8_t>(control & ~(kStopBit | kTwelveHourBit)),
    };
    const bool resumed = write_bytes(device_, resume_command, sizeof(resume_command));
    return calendar_written && resumed;
}

}  // namespace usage_panel
