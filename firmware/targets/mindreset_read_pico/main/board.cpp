#include "board.hpp"
#include "epdiy.h"
#include "esp_log.h"
#include "nvs.h"
#include "read_pico_pmu.h"
#include "read_pico_sd.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <sys/stat.h>
#include <sys/time.h>
namespace usage_panel::read_pico {
namespace {
// The three capacitive keys sit below the display on the same touch sensor.
constexpr int kKeyAreaTop = 1300, kKeyPitch = 160, kDisplayHeight = 1216;
constexpr uint64_t kHoldMs = 800, kReleaseDebounceMs = 30;
// Device-frame axis signs that read positive in the upright views: X runs along the long edge
// (keys at the bottom), Y along the short edge. The `i` serial command reports the raw sample.
constexpr int kUprightXSign = 1, kLandscapeYSign = -1;
constexpr int kMotionMg = 250;
int key_of(int y, int x) { return y >= kKeyAreaTop && x / kKeyPitch < 3 ? x / kKeyPitch + 1 : 0; }
// Days from 1970-01-01 to a proleptic Gregorian date.
constexpr int64_t days_from_civil(int year, unsigned month, unsigned day) {
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(year - era * 400);
    const unsigned doy = (153 * (month > 2 ? month - 3 : month + 9) + 2) / 5 + day - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097LL + static_cast<int>(doe) - 719468;
}
static_assert(days_from_civil(1970, 1, 1) == 0 && days_from_civil(2026, 10, 5) == 20731);
} // namespace
bool Board::begin() {
    if (read_pico_init(&hw_) != ESP_OK)
        return false;
    // The panel VCOM is calibrated at the factory and stored in the PMU.
    int vcom_mv = 0;
    for (int i = 0; i < 3 && hw_.pmu_ready; ++i)
        if (read_pico_pmu_vcom_get(&vcom_mv) == ESP_OK) {
            epd_set_vcom(static_cast<uint16_t>(vcom_mv));
            break;
        }
    if (!vcom_mv)
        ESP_LOGW("read_pico", "Panel VCOM unavailable; using the board default");
    if (hw_.sensor_ready)
        read_pico_sensor_wake(hw_.sensor);
    nvs_handle_t n;
    bool offset_valid = false;
    if (nvs_open("read_pico", NVS_READONLY, &n) == ESP_OK) {
        offset_valid = nvs_get_i16(n, "utc_off", &utc_offset_minutes_) == ESP_OK;
        nvs_close(n);
    }
    if (hw_.pmu_ready && read_pico_pmu_cmd(PMU_CMD_TIME_GET, nullptr, 0) == ESP_OK) {
        const pmu_snapshot_t *p = read_pico_pmu_get();
        if (p->time_synced && p->unix_sec > 1700000000) {
            const timeval tv{static_cast<time_t>(p->unix_sec), 0};
            settimeofday(&tv, nullptr);
            clock_set_ = offset_valid;
        }
    }
    ESP_LOGI("read_pico", "PMU=%d touch=%d accelerometer=%d clock=%d", hw_.pmu_ready,
             hw_.touch_ready, hw_.sensor_ready, clock_set_);
    return true;
}
void Board::poll(View &v) {
    read_pico_sd_info_t sd{};
    const bool sd_ok = read_pico_sd_get_info(&sd) == ESP_OK;
    // The driver mounts only on request; a card inserted after the boot probe or reinserted
    // after removal stays unmounted until a remount starts a new probe.
    if (sd.present && !sd_present_) {
        read_pico_sd_remount();
        trend_loaded_ = false;
    }
    sd_present_ = sd.present;
    v.sd = sd_ok && sd.mounted;
    v.power_valid = v.charging = external_ = false;
    v.battery = -1;
    if (hw_.pmu_ready && read_pico_pmu_poll() == ESP_OK) {
        const pmu_snapshot_t *p = read_pico_pmu_get();
        // The driver retains old power fields when STATUS fails CRC validation.
        if (p->status_ok) {
            const bool known = p->soc_permille <= 1000;
            external_ = p->charge_state == PMU_CHARGE_CHARGING ||
                        p->charge_state == PMU_CHARGE_FULL_INFERRED;
            v.power_valid = known || external_;
            v.battery = known ? (p->soc_permille + 5) / 10 : -1;
            v.charging = p->charge_state == PMU_CHARGE_CHARGING;
        }
        // Power-key events are not used; unacknowledged events would fill the PMU queue.
        if (p->pending_events)
            read_pico_pmu_drain_events();
    }
    v.clock_valid = clock_set_;
    v.utc_epoch = clock_set_ ? static_cast<uint32_t>(time(nullptr)) : 0;
    if (clock_set_) {
        const time_t local = static_cast<time_t>(v.utc_epoch) + utc_offset_minutes_ * 60;
        tm t{};
        gmtime_r(&local, &t);
        v.calendar.year = t.tm_year + 1900;
        v.calendar.month = t.tm_mon + 1;
        v.calendar.day = t.tm_mday;
        v.calendar.weekday = t.tm_wday;
        v.calendar.hour = t.tm_hour;
        v.calendar.minute = t.tm_min;
        v.calendar.second = t.tm_sec;
        v.calendar.utc_offset_minutes = utc_offset_minutes_;
    }
}
bool Board::sync(const LocalCalendarTime &c) {
    const int64_t local = days_from_civil(c.year, c.month, c.day) * 86400 + c.hour * 3600 +
                          c.minute * 60 + c.second;
    const uint32_t utc = static_cast<uint32_t>(local - c.utc_offset_minutes * 60);
    const timeval tv{static_cast<time_t>(utc), 0};
    settimeofday(&tv, nullptr);
    utc_offset_minutes_ = c.utc_offset_minutes;
    clock_set_ = true;
    const uint8_t payload[] = {static_cast<uint8_t>(utc), static_cast<uint8_t>(utc >> 8),
                               static_cast<uint8_t>(utc >> 16), static_cast<uint8_t>(utc >> 24)};
    bool ok = hw_.pmu_ready &&
              read_pico_pmu_cmd(PMU_CMD_TIME_SYNC, payload, sizeof(payload)) == ESP_OK;
    nvs_handle_t n;
    if (nvs_open("read_pico", NVS_READWRITE, &n) != ESP_OK)
        return false;
    ok = nvs_set_i16(n, "utc_off", utc_offset_minutes_) == ESP_OK && nvs_commit(n) == ESP_OK && ok;
    nvs_close(n);
    return ok;
}
Input Board::input(uint64_t now) {
    Input out;
    if (!hw_.touch_ready)
        return out;
    static struct {
        bool down = false, held = false;
        int x = 0, y = 0;
        uint64_t start = 0, released = 0;
    } state;
    cst836u_touch_t touch{};
    const bool down = cst836u_read(hw_.touch, &touch) == ESP_OK && touch.touched;
    if (down) {
        out.active = true;
        state.released = 0;
        if (!state.down)
            state = {true, false, touch.x, touch.y, now, 0};
        else if (!state.held && now - state.start >= kHoldMs) {
            state.held = true;
            if (key_of(state.y, state.x) == 2)
                out.key = 5;
        }
    } else if (state.down) {
        // The controller drops single reports while a finger rests on the glass.
        if (!state.released)
            state.released = now;
        else if (now - state.released >= kReleaseDebounceMs) {
            state.down = false;
            if (!state.held) {
                out.key = key_of(state.y, state.x);
                out.tap = state.y < kDisplayHeight;
                out.x = state.x;
                out.y = state.y;
            }
        }
    }
    return out;
}
bool Board::orientation(uint8_t &out, bool report) {
    sc7a20h_sample_t s{};
    if (!hw_.sensor_ready || read_pico_accel_read(hw_.sensor, &s) != ESP_OK)
        return false;
    const int mg[] = {s.x_mg, s.y_mg, s.z_mg};
    for (int i = 0; i < 3; ++i) {
        if (sampled_ && abs(mg[i] - last_mg_[i]) > kMotionMg)
            moved_ = true;
        last_mg_[i] = mg[i];
    }
    sampled_ = true;
    if (report)
        ESP_LOGI("read_pico", "ACCEL x=%d y=%d z=%d mg", mg[0], mg[1], mg[2]);
    const int x = abs(mg[0]), y = abs(mg[1]), z = abs(mg[2]);
    if (x * 10 < z * 7 && y * 10 < z * 7)
        return false;
    if (abs(x - y) * 100 < 15 * std::max(x, y))
        return false;
    out = x > y ? (mg[0] * kUprightXSign > 0 ? 0 : 2) : (mg[1] * kLandscapeYSign > 0 ? 1 : 3);
    return true;
}
bool Board::moved() {
    const bool moved = moved_;
    moved_ = false;
    return moved;
}
Settings Board::load() {
    Settings s;
    nvs_handle_t n;
    if (nvs_open("read_pico", NVS_READONLY, &n) == ESP_OK) {
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
    if (nvs_open("read_pico", NVS_READWRITE, &n) != ESP_OK)
        return false;
    bool ok = nvs_set_u8(n, "refresh", s.refresh) == ESP_OK &&
              nvs_set_u8(n, "idle", s.idle) == ESP_OK &&
              nvs_set_u8(n, "rotation", s.rotation) == ESP_OK && nvs_commit(n) == ESP_OK;
    nvs_close(n);
    return ok;
}
void Board::sample_trend(View &v) {
    if (!v.sd)
        return;
    if (!trend_loaded_) {
        trend_loaded_ = true;
        // The card is the store: history from a previous card is not carried onto this one.
        v.trend_count = 0;
        for (const char *path : {"/sdcard/trend.bin", "/sdcard/trend.bak"}) {
            FILE *f = fopen(path, "rb");
            if (!f)
                continue;
            bool valid = true;
            TrendPoint p;
            v.trend_count = 0;
            while (true) {
                const size_t bytes = fread(&p, 1, sizeof(p), f);
                if (!bytes)
                    break;
                if (bytes != sizeof(p) || v.trend_count == 48 ||
                    p.epoch < 1700000000 || (p.codex > 100 && p.codex != 255) ||
                    (p.claude > 100 && p.claude != 255)) {
                    valid = false;
                    break;
                }
                v.trend[v.trend_count++] = p;
            }
            valid = valid && !ferror(f);
            fclose(f);
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
    // Commit the in-memory bucket only after persistence succeeds so failed writes are retried.
    auto trend = v.trend;
    int count = v.trend_count;
    if (count == 48) {
        std::move(trend.begin() + 1, trend.end(), trend.begin());
        --count;
    }
    trend[count++] = {epoch, value(Provider::Codex), value(Provider::Claude), {}};
    FILE *f = fopen("/sdcard/trend.tmp", "wb");
    if (!f)
        return;
    bool ok = fwrite(trend.data(), sizeof(TrendPoint), count, f) == static_cast<size_t>(count);
    ok = fclose(f) == 0 && ok;
    if (!ok)
        return;
    // A failed replacement may leave only the backup; preserve it until installation succeeds.
    struct stat st;
    if (stat("/sdcard/trend.bin", &st) == 0) {
        remove("/sdcard/trend.bak");
        if (rename("/sdcard/trend.bin", "/sdcard/trend.bak") != 0)
            return;
    }
    if (rename("/sdcard/trend.tmp", "/sdcard/trend.bin") != 0) {
        ESP_LOGW("read_pico", "Trend persistence failed; backup retained");
        return;
    }
    v.trend = trend;
    v.trend_count = count;
}
} // namespace usage_panel::read_pico
