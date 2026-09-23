#pragma once

#include <atomic>
#include <cstdint>

namespace usage_panel {

/** One derived power verdict published by a board's main loop. */
struct OtaPowerReading {
    bool valid = false;
    bool external_power = false;
    uint8_t percent = 0;
};

/** Source of the latest power verdict.
 *  Implementations must be pure memory reads: read() runs on the NimBLE
 *  host task while all power hardware stays owned by each main loop. */
class OtaPowerSource {
public:
    virtual ~OtaPowerSource() = default;

    /** Loads the latest verdict. Returns false only if unreadable. */
    virtual bool read(OtaPowerReading& out) = 0;
};

constexpr uint32_t kPowerValidBit = 1u << 0;
constexpr uint32_t kPowerExternalBit = 1u << 1;
constexpr uint32_t kPowerPercentShift = 2;

inline uint32_t pack_power_word(
    bool valid, bool external_power, uint8_t percent)
{
    return (valid ? kPowerValidBit : 0u) |
           (external_power ? kPowerExternalBit : 0u) |
           (static_cast<uint32_t>(percent) << kPowerPercentShift);
}

inline OtaPowerReading unpack_power_word(uint32_t word)
{
    return {
        .valid = (word & kPowerValidBit) != 0,
        .external_power = (word & kPowerExternalBit) != 0,
        .percent = static_cast<uint8_t>((word >> kPowerPercentShift) & 0xFFu),
    };
}

/** Single-writer publication cell between a main loop and the OTA gate.
 *  The zero-initialised word reads as an invalid verdict, so an assembled
 *  but never-published source fails closed until the first publish(). */
class AtomicPowerSource final : public OtaPowerSource {
public:
    void publish(bool valid, bool external_power, uint8_t percent)
    {
        word_.store(
            pack_power_word(valid, external_power, percent),
            std::memory_order_release);
    }

    bool read(OtaPowerReading& out) override
    {
        out = unpack_power_word(word_.load(std::memory_order_acquire));
        return true;
    }

private:
    std::atomic<uint32_t> word_{0};
};

}  // namespace usage_panel
