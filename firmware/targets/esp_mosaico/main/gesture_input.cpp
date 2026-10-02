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
// Transient start-up failures are retried as in esp-vision's camera init.
constexpr unsigned kCameraStartAttempts = 3;
constexpr uint32_t kCameraRetryDelayMs = 100;
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
    uint64_t next_detection_log = 0;
    unsigned measured_frames = 0;
    uint64_t measured_us = 0;
    uint64_t capture_us = 0, detect_us = 0, classify_us = 0, feedback_us = 0;
    unsigned warmup = 0;
    // Start-up failures scroll past during boot, so the reason is repeated
    // after cleanup until the switch is turned off.
    const char* failure = nullptr;
    int failure_code = 0;
    uint64_t next_failure_log = 0;
    const auto record = [&](const char* step, int code) {
        failure = step ? step : "unknown";
        failure_code = code;
        next_failure_log = 0;
    };

    for (;;) {
        const uint32_t context = context_.load();
        const bool enabled = context & kEnabled;
        const bool allowed = context & kAllowed;
        if (context != previous_context) {
            tracker.reset(esp_timer_get_time() / 1000);
            progress_ = 0;
            previous_context = context;
        }
        if (!enabled || !allowed || fault) {
            if (active || rgb) {
                if (!camera.stop()) {
                    ESP_LOGE(TAG, "camera cleanup failed; recognition disabled until reboot");
                    record(camera.failure(), camera.failure_code());
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
            const uint64_t now_ms = esp_timer_get_time() / 1000;
            if (fault && failure && now_ms >= next_failure_log) {
                ESP_LOGW(TAG, "capture unavailable: %s (code %d)", failure, failure_code);
                next_failure_log = now_ms + 5000;
            }
            if (!enabled && !fatal_cleanup) { fault = false; failure = nullptr; status_ = GestureStatus::Off; }
            else if (fatal_cleanup) status_ = GestureStatus::Fault;
            else if (!fault) status_ = GestureStatus::Paused;
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        if (!active) {
            status_ = GestureStatus::Starting;
            // ESP-DL allocates model arenas during construction. Keep a conservative
            // margin for both arenas, two 1280x720 UYVY buffers and the concurrently running UI.
            if (heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) < 9 * 1024 * 1024 ||
                heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) < 96 * 1024) {
                // Code is the largest free PSRAM block in KiB.
                record("memory precheck",
                       int(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) / 1024));
                status_ = GestureStatus::MemoryError;
                fault = true;
                continue;
            }
            rgb = static_cast<uint8_t*>(heap_caps_malloc(kGestureImageBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
            if (!rgb) {
                record("frame buffer allocation", int(kGestureImageBytes));
                status_ = GestureStatus::MemoryError;
                fault = true;
                continue;
            }
            bool started = false;
            for (unsigned attempt = 1; attempt <= kCameraStartAttempts; ++attempt) {
                if (camera.start()) { started = true; break; }
                record(camera.failure(), camera.failure_code());
                ESP_LOGW(TAG, "camera start %u/%u failed: %s (code %d)", attempt,
                         kCameraStartAttempts, failure, failure_code);
                if (!camera.stop()) {
                    record(camera.failure(), camera.failure_code());
                    fatal_cleanup = true;
                    break;
                }
                if (attempt < kCameraStartAttempts) vTaskDelay(pdMS_TO_TICKS(kCameraRetryDelayMs));
            }
            if (!started) {
                status_ = fatal_cleanup ? GestureStatus::Fault : GestureStatus::CameraError;
                fault = true;
                continue;
            }
            detector.reset(new (std::nothrow) HandDetect(HandDetect::ESPDET_PICO_224_224_HAND, false));
            recognizer.reset(new (std::nothrow) HandGestureRecognizer());
            if (!detector || !recognizer) {
                record("model allocation", 0);
                status_ = GestureStatus::MemoryError;
                fault = true;
                continue;
            }
            detector->set_score_thr(0.5F);
            tracker.reset(esp_timer_get_time() / 1000);
            active = true;
            capture_failures = 0;
            warmup = 3;
            status_ = GestureStatus::Ready;
            ESP_LOGI(TAG, "local gestures ready; input starts after the view is clear of hands");
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
            tracker.reset(esp_timer_get_time() / 1000);
            progress_ = 0;
            if (++capture_failures >= 3) {
                record(camera.failure(), camera.failure_code());
                fault = true;
                status_ = GestureStatus::CameraError;
            }
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        capture_failures = 0;
        if (warmup) { --warmup; continue; }
        const auto image_size = camera.image_size(rotation);
        dl::image::img_t image{rgb,
            static_cast<uint16_t>(image_size.width),
            static_cast<uint16_t>(image_size.height),
            dl::image::DL_IMAGE_PIX_TYPE_RGB888};
        const uint64_t captured_us = esp_timer_get_time();
        const auto& hands = detector->run(image);
        const uint64_t detected_us = esp_timer_get_time();
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
        // A context change during the frame discards it as input; it is still previewed.
        const bool stale = context != context_.load();
        const GestureAction action = stale ? GestureAction::None
                                           : tracker.update(observation, finished_us / 1000);
        if (!stale) progress_ = tracker.hold_progress();
        if (action != GestureAction::None) {
            ESP_LOGI(TAG, "tracker action=%u", unsigned(action));
            const GestureEvent event{action, context, finished_us / 1000};
            xQueueSend(queue_, &event, 0);
        }
#if CONFIG_MOSAICO_CAMERA_PREVIEW
        // The sink waits for the display lock; events are queued first because they expire.
        if (preview_sink_) preview_sink_(preview_context_, rgb, image.width, image.height, observation);
#endif
        // Calibration aid: normalized observations in percent, at most once per second.
        if (observation.count && finished_us / 1000 >= next_detection_log) {
            ESP_LOGI(TAG, "hand count=%u score=%d center=(%d,%d) size=(%d,%d) ok=%d",
                     observation.count, int(observation.score * 100), int(observation.x * 100),
                     int(observation.y * 100), int(observation.width * 100),
                     int(observation.height * 100), int(observation.ok_score * 100));
            next_detection_log = finished_us / 1000 + 1000;
        }
        const uint64_t feedback_done_us = esp_timer_get_time();
        capture_us += captured_us - started_us;
        detect_us += detected_us - captured_us;
        classify_us += finished_us - detected_us;
        feedback_us += feedback_done_us - finished_us;
        measured_us += finished_us - started_us;
        ++measured_frames;
        if (finished_us / 1000 >= next_report) {
            ESP_LOGI(TAG, "frames=%u mean_cycle_ms=%llu internal_free=%u psram_free=%u stack_free=%u",
                measured_frames, static_cast<unsigned long long>(measured_us / measured_frames / 1000),
                unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)), unsigned(uxTaskGetStackHighWaterMark(nullptr)));
            ESP_LOGI(TAG, "mean_stage_us capture_convert=%llu detect=%llu classify=%llu feedback=%llu",
                static_cast<unsigned long long>(capture_us / measured_frames),
                static_cast<unsigned long long>(detect_us / measured_frames),
                static_cast<unsigned long long>(classify_us / measured_frames),
                static_cast<unsigned long long>(feedback_us / measured_frames));
            capture_us = detect_us = classify_us = feedback_us = 0;
            measured_us = 0;
            measured_frames = 0;
            next_report = finished_us / 1000 + 10000;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
#endif
}

}  // namespace usage_panel::mosaico
