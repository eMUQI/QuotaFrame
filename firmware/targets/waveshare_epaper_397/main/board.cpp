#include "board.hpp"
#include "driver/gpio.h"
#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "nvs.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#define XPOWERS_CHIP_AXP2101
#include "XPowersLib.h"
namespace usage_panel::epaper {
namespace {
XPowersPMU pmu;
i2c_master_dev_handle_t power_device = nullptr;
int rd(uint8_t, uint8_t reg, uint8_t *data, uint8_t size) {
    return i2c_master_transmit_receive(power_device, &reg, 1, data, size, 100) == ESP_OK ? 0 : -1;
}
int wr(uint8_t, uint8_t reg, uint8_t *data, uint8_t size) {
    uint8_t bytes[256]{reg};
    memcpy(bytes + 1, data, size);
    return i2c_master_transmit(power_device, bytes, size + 1, 100) == ESP_OK ? 0 : -1;
}
uint8_t crc(const uint8_t *data) {
    uint8_t c = 0xff;
    for (int i = 0; i < 2; ++i) {
        c ^= data[i];
        for (int j = 0; j < 8; ++j)
            c = (c & 0x80) ? (c << 1) ^ 0x31 : c << 1;
    }
    return c;
}
} // namespace
bool Board::begin() {
    i2c_master_bus_config_t c{};
    c.i2c_port = I2C_NUM_0;
    c.sda_io_num = GPIO_NUM_41;
    c.scl_io_num = GPIO_NUM_42;
    c.clk_source = I2C_CLK_SRC_DEFAULT;
    c.glitch_ignore_cnt = 7;
    c.flags.enable_internal_pullup = true;
    if (i2c_new_master_bus(&c, &bus_) != ESP_OK)
        return false;
    i2c_device_config_t d{};
    d.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    d.device_address = 0x34;
    d.scl_speed_hz = 400000;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_, &d, &power_device));
    pmu_ready_ = pmu.begin(0x34, rd, wr);
    if (!pmu_ready_)
        return false;
    pmu.setDC1Voltage(3300);
    pmu.setALDO1Voltage(3300);
    pmu.setALDO2Voltage(3300);
    pmu.setALDO3Voltage(3300);
    pmu.enableALDO1();
    pmu.enableALDO2();
    pmu.enableALDO3();
    pmu.enableBattDetection();
    pmu.enableBattVoltageMeasure();
    pmu.enableVbusVoltageMeasure();
    pmu.setPrechargeCurr(XPOWERS_AXP2101_PRECHARGE_50MA);
    pmu.setChargerConstantCurr(XPOWERS_AXP2101_CHG_CUR_200MA);
    pmu.setChargerTerminationCurr(XPOWERS_AXP2101_CHG_ITERM_25MA);
    pmu.setChargeTargetVoltage(XPOWERS_AXP2101_CHG_VOL_4V2);
    pmu.setPowerKeyPressOffTime(XPOWERS_POWEROFF_4S);
    pmu.setPowerKeyPressOnTime(XPOWERS_POWERON_1S);
    rtc_ready_ = rtc_.begin(bus_);
    d.device_address = 0x70;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_, &d, &sht_));
    const uint8_t imu_address = i2c_master_probe(bus_, QMI8658_ADDRESS_LOW, 100) == ESP_OK
                                    ? QMI8658_ADDRESS_LOW
                                    : QMI8658_ADDRESS_HIGH;
    imu_ready_ = qmi8658_init(&imu_, bus_, imu_address) == ESP_OK;
    if (imu_ready_)
        imu_ready_ = qmi8658_set_accel_range(&imu_, QMI8658_ACCEL_RANGE_2G) == ESP_OK &&
                     qmi8658_set_accel_odr(&imu_, QMI8658_ACCEL_ODR_62_5HZ) == ESP_OK &&
                     qmi8658_enable_sensors(&imu_, QMI8658_ENABLE_ACCEL) == ESP_OK;
    gpio_config_t keys{};
    keys.mode = GPIO_MODE_INPUT;
    keys.pull_up_en = GPIO_PULLUP_ENABLE;
    keys.pin_bit_mask = (1ULL << 0) | (1ULL << 4) | (1ULL << 5) | (1ULL << 6);
    ESP_ERROR_CHECK(gpio_config(&keys));
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 4;
    slot.clk = GPIO_NUM_16;
    slot.cmd = GPIO_NUM_17;
    slot.d0 = GPIO_NUM_15;
    slot.d1 = GPIO_NUM_7;
    slot.d2 = GPIO_NUM_8;
    slot.d3 = GPIO_NUM_18;
    slot.flags = SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    esp_vfs_fat_sdmmc_mount_config_t mount{};
    mount.max_files = 3;
    mount.allocation_unit_size = 16384;
    sdmmc_card_t *card = nullptr;
    sd_ = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot, &mount, &card) == ESP_OK;
    ESP_LOGI("ws397", "TG28=%d RTC=%d IMU=%d SD=%d", pmu_ready_, rtc_ready_, imu_ready_, sd_);
    return true;
}
void Board::poll(View &v) {
    v.sd = sd_;
    v.clock_valid = rtc_ready_ && rtc_.read(v.calendar);
    int p = pmu.getBatteryPercent();
    v.power_valid = pmu_ready_ && ((pmu.isVbusIn() && pmu.isVbusGood()) || (p >= 0 && p <= 100));
    v.battery = pmu.isBatteryConnect() && v.power_valid ? p : -1;
    v.charging = pmu.isCharging();
    if (v.now_ms >= environment_at_) {
        environment_at_ = v.now_ms + 300000;
        uint8_t wake[] = {0x35, 0x17}, measure[] = {0x78, 0x66}, sleep[] = {0xb0, 0x98}, data[6]{};
        bool ok = i2c_master_transmit(sht_, wake, 2, 100) == ESP_OK;
        vTaskDelay(pdMS_TO_TICKS(1));
        ok = ok && i2c_master_transmit(sht_, measure, 2, 100) == ESP_OK;
        vTaskDelay(pdMS_TO_TICKS(15));
        ok = ok && i2c_master_receive(sht_, data, 6, 100) == ESP_OK;
        i2c_master_transmit(sht_, sleep, 2, 100);
        const bool had_environment = v.environment_valid;
        v.environment_valid = ok && crc(data) == data[2] && crc(data + 3) == data[5];
        if (v.environment_valid) {
            const float temperature = -45 + 175.f * ((data[0] << 8) | data[1]) / 65536.f;
            const float humidity = 100.f * ((data[3] << 8) | data[4]) / 65536.f;
            if (!had_environment || std::abs(temperature - v.temperature) >= 0.5f)
                v.temperature = temperature;
            if (!had_environment || std::abs(humidity - v.humidity) >= 2.f)
                v.humidity = humidity;
        }
    }
}
bool Board::sync(const LocalCalendarTime &t) { return rtc_ready_ && rtc_.write(t); }
bool Board::orientation(uint8_t &out, bool report) {
    float x, y, z;
    if (!imu_ready_ || qmi8658_read_accel(&imu_, &x, &y, &z) != ESP_OK)
        return false;
    if (report)
        ESP_LOGI("ws397", "ACCEL x=%.3f y=%.3f z=%.3f", x, y, z);
    if (std::abs(x) < std::abs(z) * 0.7f && std::abs(y) < std::abs(z) * 0.7f)
        return false;
    if (std::abs(std::abs(x) - std::abs(y)) < 0.15f * std::max(std::abs(x), std::abs(y)))
        return false;
    out = std::abs(x) > std::abs(y) ? (x < 0 ? 1 : 3) : (y > 0 ? 0 : 2);
    return true;
}
int Board::key(uint64_t now) {
    struct Key {
        int pin;
        bool raw = false, down = false, held = false;
        uint64_t changed = 0, start = 0;
    };
    static Key keys[] = {{4}, {5}, {6}, {0}};
    for (int i = 0; i < 4; ++i) {
        auto &k = keys[i];
        bool down = !gpio_get_level(static_cast<gpio_num_t>(k.pin));
        if (down != k.raw) {
            k.raw = down;
            k.changed = now;
        }
        if (now - k.changed >= 30 && k.raw != k.down) {
            k.down = k.raw;
            if (k.down) {
                k.start = now;
                k.held = false;
            } else if (!k.held)
                return i + 1;
        }
        if (i == 1 && k.down && !k.held && now - k.start >= 800) {
            k.held = true;
            return 5;
        }
    }
    return 0;
}
Settings Board::load() {
    Settings s;
    nvs_handle_t n;
    if (nvs_open("ws397", NVS_READONLY, &n) == ESP_OK) {
        nvs_get_u8(n, "refresh", &s.refresh);
        nvs_get_u8(n, "idle", &s.idle);
        nvs_get_u8(n, "rotation", &s.rotation);
        nvs_close(n);
    }
    s.refresh = std::min<uint8_t>(s.refresh, 3);
    s.idle = std::min<uint8_t>(s.idle, 3);
    s.rotation = std::min<uint8_t>(s.rotation, 2);
    return s;
}
bool Board::save(const Settings &s) {
    nvs_handle_t n;
    if (nvs_open("ws397", NVS_READWRITE, &n) != ESP_OK)
        return false;
    bool ok = nvs_set_u8(n, "refresh", s.refresh) == ESP_OK &&
              nvs_set_u8(n, "idle", s.idle) == ESP_OK &&
              nvs_set_u8(n, "rotation", s.rotation) == ESP_OK && nvs_commit(n) == ESP_OK;
    nvs_close(n);
    return ok;
}
void Board::sample_trend(View &v) {
    if (!sd_)
        return;
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        for (const char *path : {"/sdcard/trend.bin", "/sdcard/trend.bak"}) {
            FILE *f = fopen(path, "rb");
            if (!f)
                continue;
            bool valid = true;
            TrendPoint p;
            v.trend_count = 0;
            while (fread(&p, sizeof(p), 1, f) == 1 && v.trend_count < 48) {
                if (p.epoch < 1700000000 || (p.codex > 100 && p.codex != 255) ||
                    (p.claude > 100 && p.claude != 255)) {
                    valid = false;
                    break;
                }
                v.trend[v.trend_count++] = p;
            }
            fclose(f);
            // A truncated write leaves no whole record, so an empty read means fall back too.
            if (valid && v.trend_count)
                break;
            v.trend_count = 0;
        }
    }
    uint32_t epoch = std::max(v.model.estimated_epoch(Provider::Codex, v.now_ms),
                              v.model.estimated_epoch(Provider::Claude, v.now_ms));
    if (!epoch || (v.trend_count && epoch / 1800 <= v.trend[v.trend_count - 1].epoch / 1800))
        return;
    if (!v.link.encrypted)
        return;
    auto value = [&](Provider p) -> uint8_t {
        const auto &s = v.model.snapshot(p);
        return s.latest_short_present && s.short_window.present ? s.short_window.used_percent : 255;
    };
    TrendPoint p{epoch, value(Provider::Codex), value(Provider::Claude), {}};
    if (v.trend_count == 48) {
        std::move(v.trend.begin() + 1, v.trend.end(), v.trend.begin());
        --v.trend_count;
    }
    v.trend[v.trend_count++] = p;
    FILE *f = fopen("/sdcard/trend.tmp", "wb");
    if (!f)
        return;
    bool ok = fwrite(v.trend.data(), sizeof(TrendPoint), v.trend_count, f) ==
              static_cast<size_t>(v.trend_count);
    ok = fclose(f) == 0 && ok;
    if (ok) {
        remove("/sdcard/trend.bak");
        rename("/sdcard/trend.bin", "/sdcard/trend.bak");
        if (rename("/sdcard/trend.tmp", "/sdcard/trend.bin") != 0)
            ESP_LOGW("ws397", "Trend persistence failed; backup retained");
    }
}
bool external_power() { return pmu.isVbusIn() && pmu.isVbusGood(); }
} // namespace usage_panel::epaper
