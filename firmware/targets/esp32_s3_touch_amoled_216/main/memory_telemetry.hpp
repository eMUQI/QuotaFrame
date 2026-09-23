#pragma once

namespace usage_panel::amoled {

/** Logs internal-heap and PSRAM availability, plus LVGL arena stats when ready. */
void log_memory_checkpoint(const char* stage, bool lvgl_ready);

/**
 * Allocates a small LVGL probe and verifies that the configured LVGL allocator
 * actually returns PSRAM. This is a startup invariant, not a capacity test.
 */
bool verify_lvgl_allocations_external();

}  // namespace usage_panel::amoled
