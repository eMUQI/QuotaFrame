#pragma once

#include <cstdint>

#include "esp_desktop_buddy/esp_desktop_buddy.h"
#include "usage_core/usage_state.hpp"

namespace usage_panel {

/** Outcome of reading one command field. */
enum class FieldResult { Missing, Valid, Invalid };

/**
 * Reads one decimal uint32 field in its canonical text form.
 *
 * Leading zeros, signs, non-digits and values above UINT32_MAX are rejected,
 * so every command sees the same wire contract.
 */
FieldResult get_decimal_u32(
    const esp_desktop_buddy_command_view_t* request,
    const char* key, uint32_t& value);

/** Result of decoding one usage version 1 command. */
struct UsageCommandParseResult {
    bool valid = false;
    uint32_t sequence = 0;
    UsageUpdate update{};
};

/**
 * Parses the privacy-allowlisted usage version 1 wire contract.
 *
 * Decimal integers must use their canonical text form, percentages must be
 * 0-100, reset timestamps may only accompany their corresponding windows,
 * and state must match the number of windows present. sent_at may precede
 * sampled_at by at most five minutes to tolerate limited clock skew.
 */
UsageCommandParseResult parse_usage_command(
    const esp_desktop_buddy_command_view_t* request);

}  // namespace usage_panel
