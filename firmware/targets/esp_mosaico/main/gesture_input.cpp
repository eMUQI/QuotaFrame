#include "gesture_input.hpp"
#include "sdkconfig.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/task.h"

#if CONFIG_MOSAICO_GESTURE_INPUT
#include <cstring>
#include <memory>
#include <new>
#include "esp_heap_caps.h"
#include "gesture_camera.hpp"
#include "gesture_image.hpp"
#include "hand_detect.hpp"
#include "hand_gesture_recognition.hpp"
#endif

namespace usage_panel::mosaico {
#if CONFIG_MOSAICO_GESTURE_INPUT
static_assert(CONFIG_MOSAICO_CAMERA_ROTATION % 90 == 0, "Camera rotation must be a multiple of 90 degrees");
#endif
namespace {
constexpr uint32_t kEnabled = 1;
constexpr uint32_t kAllowed = 2;
constexpr uint32_t kAsleep = 4;
constexpr uint32_t kContextMask = 0x7f;
constexpr uint32_t kGeneration = 0x80;
constexpr char TAG[] = "gesture_input";
}

bool GestureInput::begin()
{
#if CONFIG_MOSAICO_GESTURE_INPUT
    queue_ = xQueueCreate(1, sizeof(GestureEvent));
    if (!queue_) { status_ = GestureStatus::MemoryError; return false; }
    if (xTaskCreate(task_entry, "hand_input", 8192, this, 3, nullptr) != pdPASS) {
        vQueueDelete(queue_);
        queue_ = nullptr;
        status_ = GestureStatus::MemoryError;
        return false;
    }
#endif
    return true;
}

void GestureInput::configure(bool enabled, bool allowed, bool asleep, Page page, ScreenRotation rotation)
{
    const uint32_t flags = (enabled ? kEnabled : 0) | (allowed ? kAllowed : 0) |
        (asleep ? kAsleep : 0) | (uint32_t(rotation) << 3) | (uint32_t(page) << 5);
    const uint32_t old = context_.load();
    if ((old & kContextMask) != flags) {
        context_ = ((old + kGeneration) & ~kContextMask) | flags;
        progress_ = 0;
    }
}

void GestureInput::invalidate()
{
    context_.fetch_add(kGeneration);
    progress_ = 0;
}

bool GestureInput::take(GestureEvent& event, uint64_t now_ms)
{
    if (!queue_ || xQueueReceive(queue_, &event, 0) != pdTRUE) return false;
    const uint32_t context = context_.load();
    return event.context == context && (context & (kEnabled | kAllowed)) == (kEnabled | kAllowed) &&
        now_ms >= event.time_ms && now_ms - event.time_ms <= 300;
}

void GestureInput::task_entry(void* context)
{
    static_cast<GestureInput*>(context)->run();
    vTaskDelete(nullptr);
}

void GestureInput::run()
{
#if CONFIG_MOSAICO_GESTURE_INPUT
    GestureCamera camera;
    GestureTracker tracker;
    std::unique_ptr<HandDetect> detector;
    std::unique_ptr<HandGestureRecognizer> recognizer;
    uint8_t* rgb = nullptr;
    uint32_t previous_context = UINT32_MAX;
    bool active = false;
    bool fault = false;
    bool fatal_cleanup = false;
    unsigned capture_failures = 0;
    uint64_t next_report = 0;
    unsigned measured_frames = 0;
    uint64_t measured_us = 0;
    unsigned warmup = 0;

    for (;;) {
        const uint32_t context = context_.load();
        const bool enabled = context & kEnabled;
        const bool allowed = context & kAllowed;
        if (context != previous_context) {
            tracker.reset();
            progress_ = 0;
            previous_context = context;
        }
        if (!enabled || !allowed || fault) {
            if (active || rgb) {
                if (!camera.stop()) {
                    ESP_LOGE(TAG, "camera cleanup failed; recognition disabled until reboot");
                    fatal_cleanup = true;
                    fault = true;
                }
                recognizer.reset();
                detector.reset();
                heap_caps_free(rgb);
                rgb = nullptr;
                active = false;
            }
            progress_ = 0;
            if (!enabled && !fatal_cleanup) { fault = false; status_ = GestureStatus::Off; }
            else if (fatal_cleanup) status_ = GestureStatus::Fault;
            else if (!fault) status_ = GestureStatus::Paused;
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        if (!active) {
            status_ = GestureStatus::Starting;
            // ESP-DL allocates model arenas during construction. Keep a conservative
            // margin for both arenas, camera buffers and the concurrently running UI.
            if (heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) < 6 * 1024 * 1024 ||
                heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) < 96 * 1024) {
                status_ = GestureStatus::MemoryError;
                fault = true;
                continue;
            }
            rgb = static_cast<uint8_t*>(heap_caps_malloc(kGestureImageBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
            if (!rgb) { status_ = GestureStatus::MemoryError; fault = true; continue; }
            if (!camera.start()) {
                status_ = GestureStatus::CameraError;
                fault = true;
                continue;
            }
            detector.reset(new (std::nothrow) HandDetect(HandDetect::ESPDET_PICO_224_224_HAND, false));
            recognizer.reset(new (std::nothrow) HandGestureRecognizer());
            if (!detector || !recognizer) { status_ = GestureStatus::MemoryError; fault = true; continue; }
            detector->set_score_thr(0.5F);
            tracker.reset();
            active = true;
            capture_failures = 0;
            warmup = 3;
            status_ = GestureStatus::Ready;
            ESP_LOGI(TAG, "local gestures ready; remove hand before starting a new action");
        }
        const uint64_t started_us = esp_timer_get_time();
        // Display rotation maps logical pixels to physical coordinates; camera
        // observations require the inverse transform after the mounting correction.
        const unsigned display_rotation = (context >> 3) & 3;
        const unsigned rotation = (CONFIG_MOSAICO_CAMERA_ROTATION / 90 + 4 - display_rotation) % 4;
#ifdef CONFIG_MOSAICO_CAMERA_MIRROR
        const bool mirror = true;
#else
        const bool mirror = false;
#endif
        if (!camera.read_rgb(rgb, rotation, mirror)) {
            tracker.reset();
            progress_ = 0;
            if (++capture_failures >= 3) { fault = true; status_ = GestureStatus::CameraError; }
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        capture_failures = 0;
        if (warmup) { --warmup; continue; }
        if (context != context_.load()) continue;
        dl::image::img_t image{rgb,
            static_cast<uint16_t>(rotation & 1 ? kGestureImageHeight : kGestureImageWidth),
            static_cast<uint16_t>(rotation & 1 ? kGestureImageWidth : kGestureImageHeight),
            dl::image::DL_IMAGE_PIX_TYPE_RGB888};
        const auto& hands = detector->run(image);
        HandObservation observation{};
        observation.count = hands.size();
        if (hands.size() == 1) {
            const auto& hand = hands.front();
            if (hand.box.size() == 4) {
                observation.x = (hand.box[0] + hand.box[2]) * 0.5F / image.width;
                observation.y = (hand.box[1] + hand.box[3]) * 0.5F / image.height;
                observation.width = float(hand.box[2] - hand.box[0]) / image.width;
                observation.height = float(hand.box[3] - hand.box[1]) / image.height;
                observation.score = hand.score;
                if (!(context & kAsleep) && hand.score >= 0.65F &&
                    hand.box[0] >= 0 && hand.box[1] >= 0 && hand.box[2] <= image.width &&
                    hand.box[3] <= image.height && hand.box[2] > hand.box[0] && hand.box[3] > hand.box[1]) {
                    const auto classes = recognizer->recognize(image, hands);
                    for (const auto& result : classes) {
                        if (result.cat_name && std::strcmp(result.cat_name, "ok") == 0)
                            observation.ok_score = result.score;
                    }
                }
            }
        }
        const uint64_t finished_us = esp_timer_get_time();
        if (context != context_.load()) continue;
        const GestureAction action = tracker.update(observation, finished_us / 1000);
        progress_ = tracker.hold_progress();
        if (action != GestureAction::None) {
            const GestureEvent event{action, context, finished_us / 1000};
            xQueueSend(queue_, &event, 0);
        }
        measured_us += finished_us - started_us;
        ++measured_frames;
        if (finished_us / 1000 >= next_report) {
            ESP_LOGI(TAG, "frames=%u mean_cycle_ms=%llu internal_free=%u psram_free=%u stack_free=%u",
                measured_frames, static_cast<unsigned long long>(measured_us / measured_frames / 1000),
                unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)), unsigned(uxTaskGetStackHighWaterMark(nullptr)));
            measured_us = 0;
            measured_frames = 0;
            next_report = finished_us / 1000 + 10000;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
#endif
}

}  // namespace usage_panel::mosaico
