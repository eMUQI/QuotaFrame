#pragma once

#include <cstdint>

#include "usage_panel_state/page_state.hpp"

namespace usage_panel {

/** Render-facing projection of screensaver state. */
struct ScreensaverView {
    bool active = false;
    uint8_t hint_opacity = 0;
    int8_t offset_x = 0;
    int8_t offset_y = 0;
    bool consume_touch = false;

    bool operator==(const ScreensaverView& other) const;
};

/**
 * Applies AMOLED idle policy without owning input or rendering.
 *
 * Pairing and OTA keep the screen awake. The touch that wakes the panel is
 * consumed behind a short guard so it cannot also activate the restored UI.
 * While active, periodic small offsets provide burn-in mitigation.
 */
class ScreensaverController {
public:
    static constexpr uint64_t kDefaultIdleDelayMs = 300000;

    /**
     * The application supplies the timeout; zero disables automatic entry.
     */
    explicit ScreensaverController(
        uint64_t idle_delay_ms = kDefaultIdleDelayMs);

    void reset(uint64_t now_ms, Page page);
    /** Zero disables automatic entry; explicit toggles remain available. */
    void set_idle_delay(uint64_t delay_ms) { idle_delay_ms_ = delay_ms; }
    void note_touch_down(uint64_t now_ms);
    void note_shake(uint64_t now_ms);
    /**
     * Toggles the screensaver from a deliberate PWR key press.
     *
     * Locking this way suppresses touch and shake wake briefly, so the motion
     * of pressing the key cannot immediately undo it. A further PWR press is
     * always honoured, and the guard never applies while already awake.
     */
    void note_power_key(uint64_t now_ms, Page current_page);
    /** Toggles the screensaver without suppressing physical wake inputs. */
    void note_remote_toggle(uint64_t now_ms, Page current_page);
    void update(
        uint64_t now_ms, Page current_page, bool pairing, bool ota);
    ScreensaverView view(uint64_t now_ms) const;
    bool touch_allowed(uint64_t now_ms) const;
    Page restore_page() const;

private:
    uint32_t next_random();
    void enter(uint64_t now_ms, Page current_page);
    void exit(uint64_t now_ms);
    void choose_offset();

    bool active_ = false;
    bool consume_touch_ = false;
    uint64_t last_activity_ms_ = 0;
    uint64_t active_since_ms_ = 0;
    uint64_t next_shift_ms_ = 0;
    uint64_t ignore_touch_until_ms_ = 0;
    uint64_t ignore_wake_until_ms_ = 0;
    Page restore_page_ = Page::Overview;
    uint64_t idle_delay_ms_ = kDefaultIdleDelayMs;
    int8_t offset_x_ = 0;
    int8_t offset_y_ = 0;
    uint32_t random_state_ = 0x6D2B79F5U;
};

/** Detects repeated acceleration-magnitude excursions for shake-to-wake. */
class ShakeDetector {
public:
    explicit ShakeDetector(float excursion_threshold_g = 0.25F);

    bool update(float ax_g, float ay_g, float az_g, uint64_t now_ms);

private:
    float excursion_threshold_g_;
    bool has_sample_ = false;
    bool has_event_ = false;
    uint64_t sample_ms_ = 0;
    uint64_t event_ms_ = 0;
};

// Detects a sustained rotation of the gravity direction away from a slowly
// adapting resting reference. Picking the panel up reorients gravity by tens of
// degrees, while zero-mean desk vibration only wobbles it by a few degrees.
class TiltWakeDetector {
public:
    explicit TiltWakeDetector(float wake_degrees = 25.0F);

    bool update(float ax_g, float ay_g, float az_g);

private:
    float rest_x_ = 0.0F;
    float rest_y_ = 0.0F;
    float rest_z_ = 1.0F;
    bool armed_ = false;
    uint8_t above_frames_ = 0;
    const float wake_cos_;

    static constexpr float kSlowAlpha = 0.02F;
    static constexpr uint8_t kConfirmFrames = 2;
};

}  // namespace usage_panel
