#pragma once
#include "M5Unified.h"
#include "battery_view.hpp"
#include "page_state.hpp"
#include "usage_core/usage_state.hpp"
#include "usage_ota/session.hpp"

namespace usage_panel {
class DisplayUi {
public:
    void begin();
    void render(const UsageModel& model, Page page, bool connected,
                bool has_passkey, uint32_t passkey, uint64_t now_ms,
                const OtaSnapshot& ota, uint32_t confirm_seconds,
                bool show_ota_error,
                const m5::BatteryView& battery);
private:
    lgfx::LovyanGFX& target() { return *draw_target_; }
    void provider_card(const UsageModel&, Provider, int y, bool connected, uint64_t now_ms);
    void provider_page(const UsageModel&, Provider, bool connected, uint64_t now_ms);
    void progress(int x, int y, int width, uint8_t percent, uint16_t color);
    uint16_t usage_color(uint8_t percent) const;
    void ota_page(const OtaSnapshot& ota, uint32_t confirm_seconds,
                  bool show_error);
    void battery_icon(const m5::BatteryView& battery);

    M5Canvas canvas_{&M5.Display};
    lgfx::LovyanGFX* draw_target_ = nullptr;
    bool canvas_ready_ = false;
};
}
