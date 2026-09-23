#pragma once

#include "sdkconfig.h"

#if CONFIG_WS_USAGE_PANEL_PERFORMANCE_PROBE
#include <cstdint>
#include "esp_lcd_panel_io.h"
#include "lvgl.h"

namespace usage_panel::amoled {

/** Installs diagnostic callbacks before the LVGL task starts. */
bool attach_performance_probe(lv_display_t* display, esp_lcd_panel_io_handle_t io);

/** Drains display samples and records resources from the application task. */
void poll_performance_probe(uint32_t page, bool encrypted, uint32_t ota_phase,
                            uint32_t ota_bytes);

}  // namespace usage_panel::amoled
#endif
