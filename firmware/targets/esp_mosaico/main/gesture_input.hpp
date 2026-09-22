#pragma once

#include <atomic>
#include <cstdint>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "gesture_tracker.hpp"
#include "orientation.hpp"

namespace usage_panel::mosaico {

enum class GestureStatus : uint8_t { Off, Starting, Ready, Paused, CameraError, MemoryError, Fault };

struct GestureEvent {
    GestureAction action = GestureAction::None;
    uint32_t context = 0;
    uint64_t time_ms = 0;
};

/** Worker-owned camera/model pipeline. The application owns context and event consumption. */
class GestureInput {
public:
    bool begin();
    void configure(bool enabled, bool allowed, bool asleep, Page page, ScreenRotation rotation);
    /** Cancels pending recognition after another input is accepted. Main task only. */
    void invalidate();
    bool take(GestureEvent& event, uint64_t now_ms);
    GestureStatus status() const { return status_.load(); }
    uint8_t progress() const { return progress_.load(); }

private:
    static void task_entry(void* context);
    void run();
    QueueHandle_t queue_ = nullptr;
    std::atomic<uint32_t> context_{0};
    std::atomic<GestureStatus> status_{GestureStatus::Off};
    std::atomic<uint8_t> progress_{0};
};

}  // namespace usage_panel::mosaico
