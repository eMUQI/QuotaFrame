#pragma once
#include "display_ui.hpp"
#include "read_pico_init.h"
namespace usage_panel::read_pico {
struct Input {
    // 1-3: KEY1-KEY3 released before the hold time; 5: KEY2 held for 800 ms.
    int key = 0;
    // A touch on the display released before the hold time, in upright portrait coordinates.
    bool tap = false;
    int x = 0, y = 0;
    bool active = false;
};
class Board {
  public:
    /** Requires nvs_flash_init(). Returns false only when the panel cannot start. */
    bool begin();
    EpdiyHighlevelState *display() { return &hw_.hl; }
    void poll(View &view);
    /** Sets the system clock and the PMU RTC, and stores the UTC offset. */
    bool sync(const LocalCalendarTime &time);
    Input input(uint64_t now);
    /** Samples the accelerometer; false when the device lies flat or reads diagonally. */
    bool orientation(uint8_t &out, bool report = false);
    /** True once after an orientation() sample differed from the one before it. */
    bool moved();
    bool external_power() const { return external_; }
    bool save(const Settings &settings);
    Settings load();
    void sample_trend(View &view);

  private:
    read_pico_handle_t hw_{};
    // The PMU RTC counts UTC seconds; the wall clock needs the offset from the last time sync.
    int16_t utc_offset_minutes_ = 0;
    bool clock_set_ = false, external_ = false, moved_ = false, sampled_ = false;
    int last_mg_[3]{};
};
} // namespace usage_panel::read_pico
