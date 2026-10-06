#pragma once
#include "display_ui.hpp"
namespace usage_panel::read_pico {
/**
 * Builds a synthetic view of one design board for display checks without a Bridge.
 * command '1'-'9': home, alarms, clock, landscape home, pairing, OTA progress, settings,
 * offline, OTA confirmation; 't': gray-level test card. Settings, uptime and the device name are taken from live.
 */
View demo_view(char command, const View &live);
} // namespace usage_panel::read_pico
