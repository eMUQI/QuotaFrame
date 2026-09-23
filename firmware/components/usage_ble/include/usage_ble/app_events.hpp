#pragma once

#include <cstddef>
#include <cstdint>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "usage_core/usage_state.hpp"
#include "usage_protocol/time_sync.hpp"

namespace usage_panel {

enum class AppEventType : uint8_t { Usage, Link, TimeSync, ScreenToggle, ScreenPage };

/** Latest BLE link/security state published to the application loop. */
struct LinkUpdate {
    bool connected;
    bool encrypted;
    bool has_passkey;
    uint32_t passkey;
};

/** Calendar update delivered after a validated time_sync command. */
struct TimeSyncUpdate {
    uint32_t sequence;
    LocalCalendarTime calendar;
};

struct AppEvent {
    AppEventType type;
    UsageUpdate usage;
    LinkUpdate link;
    TimeSyncUpdate time_sync;
    bool previous_page = false;
};

/**
 * Fixed-size FreeRTOS queue that separates BLE callbacks from application work.
 *
 * Producers never block: send_* returns false when the queue has not been
 * created or is full, allowing the command layer to return queue_full instead
 * of stalling a transport callback.
 */
class AppEventQueue {
public:
    /** Allocates the queue. Returns false if allocation fails. */
    bool begin(size_t depth = 8);
    bool send_usage(const UsageUpdate& update);
    bool send_link(const LinkUpdate& update);
    bool send_time_sync(const TimeSyncUpdate& update);
    bool send_screen_toggle();
    bool send_screen_page(bool previous);

    /** Receives one event, waiting at most `wait` FreeRTOS ticks. */
    bool receive(AppEvent& event, TickType_t wait = 0);

private:
    QueueHandle_t queue_ = nullptr;
};

}  // namespace usage_panel
