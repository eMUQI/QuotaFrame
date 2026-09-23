#pragma once

#include <cstddef>
#include <cstdint>

#include "usage_ota/session.hpp"

namespace usage_panel {

/**
 * Builds `<prefix><MAC-suffix>` into the caller-provided buffer.
 *
 * @return false for invalid arguments or truncation; true on success.
 */
bool make_advertising_name(
    const char* prefix, uint16_t mac_suffix,
    char* output, size_t output_size);

/**
 * Builds the public status object returned during Bridge capability negotiation.
 *
 * Names follow the public UTF-8 contract; JSON strings are escaped by cJSON.
 * @return false for invalid names, allocation failure, or insufficient output space.
 */
bool make_status_json(
    const char* status_name, bool encrypted, const char* ui_location,
    const char* firmware_version, const char* target,
    const OtaSnapshot& ota, bool enable_time_sync,
    char* output, size_t output_size, bool enable_screen_toggle = false,
    bool boot_valid = false, bool enable_screen_page = false);

}  // namespace usage_panel
