#pragma once
#define READ_PICO_EPD_PCLK_MIN_MHZ 12
#define READ_PICO_EPD_SCAN_FULL 0
static inline void read_pico_epd_use_scan(int) {}
static inline void read_pico_epd_set_pclk(int) {}
