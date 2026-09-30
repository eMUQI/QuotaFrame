#pragma once
#include "driver/i2c_master.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_oneshot.h"
#include "panel_logic.hpp"
#include "usage_protocol/time_sync.hpp"
#include "usage_rtc/rtc_clock.hpp"
#include "view.hpp"
namespace usage_panel::rlcd {
/** ESP32-S3-RLCD-4.2 peripherals other than the display and the speaker. */
class Board {
  public:
    /** Brings up I2C, RTC, SHTC3, keys, battery ADC and the TF card. Optional parts may fail. */
    bool begin();
    i2c_master_bus_handle_t bus() const { return bus_; }
    /** Reads the clock every call, the battery every 10 s and the environment every 60 s. */
    void poll(View &view);
    bool sync(const LocalCalendarTime &time);
    /** Debounced KEY (GPIO18) and BOOT (GPIO0); holding KEY for 800 ms yields KeyHold. */
    Input key(uint64_t now_ms);
    bool save(const Settings &settings);
    Settings load();
    /** Appends one 30-minute usage sample to the TF card trend log when due. */
    void sample_trend(View &view);

  private:
    i2c_master_bus_handle_t bus_ = nullptr;
    i2c_master_dev_handle_t sht_ = nullptr;
    RtcClock rtc_;
    adc_oneshot_unit_handle_t adc_ = nullptr;
    adc_cali_handle_t cali_ = nullptr;
    bool rtc_ready_ = false, sd_ = false, trend_loaded_ = false;
    uint64_t environment_at_ = 0, battery_at_ = 0;
    void read_environment(View &view);
    void read_battery(View &view);
};
} // namespace usage_panel::rlcd
