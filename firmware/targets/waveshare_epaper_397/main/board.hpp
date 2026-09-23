#pragma once
#include "display_ui.hpp"
#include "driver/i2c_master.h"
#include "qmi8658.h"
#include "usage_rtc/rtc_clock.hpp"
namespace usage_panel::epaper {
class Board {
  public:
    bool begin();
    void poll(View &view);
    bool sync(const LocalCalendarTime &time);
    int key(uint64_t now);
    bool orientation(uint8_t &out, bool report = false);
    bool save(const Settings &settings);
    Settings load();
    void sample_trend(View &view);

  private:
    i2c_master_bus_handle_t bus_ = nullptr;
    i2c_master_dev_handle_t sht_ = nullptr;
    RtcClock rtc_;
    qmi8658_dev_t imu_{};
    bool imu_ready_ = false, rtc_ready_ = false, pmu_ready_ = false, sd_ = false;
    uint64_t environment_at_ = 0;
};
} // namespace usage_panel::epaper
