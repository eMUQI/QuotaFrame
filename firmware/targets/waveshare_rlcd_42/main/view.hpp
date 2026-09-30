#pragma once
#include "usage_core/usage_state.hpp"
#include <array>
#include <cstdint>
// The view and renderer depend only on usage_core so the screens can be rendered on a host.
namespace usage_panel::rlcd {
/** Pages in navigation order; Settings sits outside the KEY ring. */
enum class Page : uint8_t { Home, Codex, Claude, Trend, Clock, Settings };
constexpr int kRingPages = 5;

enum class Rotation : uint8_t { Landscape, Portrait, Flipped };
enum class AlertMode : uint8_t { Off, Invert, Beep };

/** Persisted display settings; values index the option lists shown on the Settings page. */
struct Settings {
    uint8_t cycle = 0;    // Auto page cycle: OFF, 30 s, 1 min, 5 min.
    uint8_t alert = 2;    // AlertMode.
    uint8_t seconds = 1;  // 1 shows seconds on countdowns and the clock.
    uint8_t rotation = 0; // Rotation.
};
constexpr int kSettingRows = 4;

/** One 30-minute trend record as stored on the TF card; 255 marks a missing value. */
struct TrendPoint {
    uint32_t epoch;
    uint8_t codex, claude;
    uint8_t reserved[2]{};
};
constexpr int kTrendPoints = 48;
constexpr uint32_t kTrendIntervalSeconds = 1800;
constexpr uint32_t kMinTrendEpoch = 1700000000;

struct LinkState {
    bool connected = false, encrypted = false, has_passkey = false;
    uint32_t passkey = 0;
};

enum class OtaStage : uint8_t { Idle, Confirming, Receiving, Verifying, Rebooting };
struct OtaStatus {
    OtaStage stage = OtaStage::Idle;
    uint32_t offset = 0, size = 0;
    char version[24]{};
    const char *error = nullptr; // Title of a recent failure while idle; null when none.
};

/** Local wall-clock time read from the RTC. */
struct ClockTime {
    bool valid = false;
    uint16_t year = 0;
    uint8_t month = 0, day = 0, weekday = 0, hour = 0, minute = 0, second = 0;
};

struct View {
    UsageModel model;
    LinkState link;
    ClockTime clock;
    bool environment_valid = false;
    float temperature = 0, humidity = 0;
    int battery = -1; // Percent, or -1 when unknown.
    Page page = Page::Home;
    Settings settings;
    int focus = 0;
    bool save_error = false;
    std::array<uint8_t, 2> warning{}; // Per provider: 0 normal, 1 >= 80 %, 2 >= 95 %.
    int alert = -1;                   // Provider index whose full-screen alert is shown.
    bool sd = false;
    std::array<TrendPoint, kTrendPoints> trend{};
    int trend_count = 0;
    OtaStatus ota;
    uint64_t now_ms = 0;
    char device_name[24]{};
};
} // namespace usage_panel::rlcd
