#include "performance_probe.hpp"

#if CONFIG_WS_USAGE_PANEL_PERFORMANCE_PROBE
#include <algorithm>
#include <cinttypes>

#include "display_transport.hpp"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_lv_adapter.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

namespace usage_panel::amoled {
namespace {
constexpr char TAG[] = "performance";
constexpr uint32_t INTERNAL = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
constexpr uint32_t DMA = MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA;
constexpr size_t TASK_CAPACITY = 48;
static_assert(!make_display_buffer_plan().require_double_buffer,
              "performance probe requires a single in-flight draw buffer");
#if !CONFIG_FREERTOS_RUN_TIME_STATS_USING_ESP_TIMER
#error "Performance samples require the ESP Timer runtime clock"
#endif

struct DisplaySample {
    int64_t start_us;
    int64_t first_flush_us;
    int64_t end_us;
    uint32_t id;
    uint32_t bytes;
    uint32_t strips;
    bool dma_done;
};

QueueHandle_t samples;
TaskStatus_t* tasks;
portMUX_TYPE display_mux = portMUX_INITIALIZER_UNLOCKED;
DisplaySample current{};
DisplaySample pending{};
bool pending_last;
uint32_t dropped;

void display_event(lv_event_t* event)
{
    auto* display = static_cast<lv_display_t*>(lv_event_get_target(event));
    const auto code = lv_event_get_code(event);
    const int64_t now = esp_timer_get_time();
    DisplaySample ready{};
    portENTER_CRITICAL(&display_mux);
    if (code == LV_EVENT_REFR_START) {
        const uint32_t id = current.id + 1;
        current = {};
        current.id = id;
        current.start_us = now;
    } else if (code == LV_EVENT_FLUSH_START) {
        const auto* area = static_cast<const lv_area_t*>(lv_event_get_param(event));
        current.bytes += lv_area_get_size(area) * 2;
        if (current.strips++ == 0) current.first_flush_us = now;
        pending = current;
        pending_last = lv_display_flush_is_last(display);
    } else if (code == LV_EVENT_REFR_READY && current.bytes != 0) {
        ready = current;
        ready.end_us = now;
    }
    portEXIT_CRITICAL(&display_mux);
    if (ready.bytes != 0 && xQueueSend(samples, &ready, 0) != pdTRUE) {
        portENTER_CRITICAL(&display_mux);
        ++dropped;
        portEXIT_CRITICAL(&display_mux);
    }
}

bool IRAM_ATTR color_done(esp_lcd_panel_io_handle_t,
                         esp_lcd_panel_io_event_data_t*, void* context)
{
    BaseType_t woken = pdFALSE;
    DisplaySample done{};
    portENTER_CRITICAL_ISR(&display_mux);
    if (pending_last) {
        done = pending;
        done.end_us = esp_timer_get_time();
        done.dma_done = true;
        pending_last = false;
    }
    portEXIT_CRITICAL_ISR(&display_mux);
    if (done.bytes != 0 && xQueueSendFromISR(samples, &done, &woken) != pdTRUE) {
        portENTER_CRITICAL_ISR(&display_mux);
        ++dropped;
        portEXIT_CRITICAL_ISR(&display_mux);
    }
    // The adapter retains ownership of the LVGL flush-ready handshake.
    const bool adapter_woken = esp_lv_adapter_display_notify_color_trans_done_from_isr(
        static_cast<lv_display_t*>(context));
    return adapter_woken || woken == pdTRUE;
}
}  // namespace

bool attach_performance_probe(lv_display_t* display, esp_lcd_panel_io_handle_t io)
{
    // The pending strip record relies on the target's single draw buffer.
    samples = xQueueCreate(32, sizeof(DisplaySample));
    tasks = static_cast<TaskStatus_t*>(heap_caps_calloc(
        TASK_CAPACITY, sizeof(TaskStatus_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (samples == nullptr || tasks == nullptr) return false;
    const esp_lcd_panel_io_callbacks_t callbacks{.on_color_trans_done = color_done};
    if (esp_lcd_panel_io_register_event_callbacks(io, &callbacks, display) != ESP_OK) {
        return false;
    }
    lv_display_add_event_cb(display, display_event, LV_EVENT_REFR_START, nullptr);
    lv_display_add_event_cb(display, display_event, LV_EVENT_FLUSH_START, nullptr);
    lv_display_add_event_cb(display, display_event, LV_EVENT_REFR_READY, nullptr);
    ESP_LOGI(TAG, "PERF_CONFIG queue_bytes=%u task_array_psram_bytes=%u",
             static_cast<unsigned>(32 * sizeof(DisplaySample)),
             static_cast<unsigned>(TASK_CAPACITY * sizeof(TaskStatus_t)));
    return true;
}

void poll_performance_probe(uint32_t page, bool encrypted, uint32_t ota_phase,
                            uint32_t ota_bytes)
{
    static int64_t previous_poll;
    static int64_t next_resources;
    static int64_t next_tasks;
    static int64_t max_poll_gap;
    const int64_t now = esp_timer_get_time();
    if (previous_poll != 0) max_poll_gap = std::max(max_poll_gap, now - previous_poll);
    previous_poll = now;

    DisplaySample sample{};
    for (unsigned i = 0; i < 32 && xQueueReceive(samples, &sample, 0) == pdTRUE; ++i) {
        ESP_LOGI(TAG, "PERF_DISPLAY id=%" PRIu32 " kind=%s start_us=%" PRId64
                 " first_flush_us=%" PRId64 " end_us=%" PRId64
                 " bytes=%" PRIu32 " strips=%" PRIu32,
                 sample.id, sample.dma_done ? "dma" : "render", sample.start_us,
                 sample.first_flush_us, sample.end_us, sample.bytes, sample.strips);
    }
    if (now < next_resources) return;
    next_resources = now + 1000000;
    uint32_t lost;
    portENTER_CRITICAL(&display_mux);
    lost = dropped;
    portEXIT_CRITICAL(&display_mux);
    ESP_LOGI(TAG, "PERF_RESOURCE t_us=%" PRId64 " page=%" PRIu32
             " encrypted=%d ota_phase=%" PRIu32 " ota_bytes=%" PRIu32
             " internal_free=%u internal_largest=%u internal_min=%u"
             " dma_free=%u dma_largest=%u psram_free=%u"
             " poll_gap_max_us=%" PRId64 " dropped=%" PRIu32,
             now, page, encrypted, ota_phase, ota_bytes,
             static_cast<unsigned>(heap_caps_get_free_size(INTERNAL)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(INTERNAL)),
             static_cast<unsigned>(heap_caps_get_minimum_free_size(INTERNAL)),
             static_cast<unsigned>(heap_caps_get_free_size(DMA)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(DMA)),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
             max_poll_gap, lost);
    max_poll_gap = 0;
    if (esp_lv_adapter_lock(0) == ESP_OK) {
        lv_mem_monitor_t memory{};
        lv_mem_monitor(&memory);
        esp_lv_adapter_unlock();
        ESP_LOGI(TAG, "PERF_LVGL t_us=%" PRId64 " total=%u free=%u largest=%u"
                 " max_used=%u frag_pct=%u", now,
                 static_cast<unsigned>(memory.total_size),
                 static_cast<unsigned>(memory.free_size),
                 static_cast<unsigned>(memory.free_biggest_size),
                 static_cast<unsigned>(memory.max_used), memory.frag_pct);
    } else {
        ESP_LOGI(TAG, "PERF_LVGL t_us=%" PRId64 " skipped=lock_busy", now);
    }
    if (now < next_tasks) return;
    next_tasks = now + 5000000;
    configRUN_TIME_COUNTER_TYPE total{};
    const int64_t snapshot_start = esp_timer_get_time();
    const auto count = uxTaskGetSystemState(tasks, TASK_CAPACITY, &total);
    ESP_LOGI(TAG, "PERF_TASK_SNAPSHOT t_us=%" PRId64 " total_us=%llu count=%u"
             " capacity=%u snapshot_us=%" PRId64, now,
             static_cast<unsigned long long>(total), static_cast<unsigned>(count),
             static_cast<unsigned>(TASK_CAPACITY), esp_timer_get_time() - snapshot_start);
    for (UBaseType_t i = 0; i < count; ++i) {
        const auto& task = tasks[i];
        ESP_LOGI(TAG, "PERF_TASK t_us=%" PRId64 " id=%u name=%s core=%d"
                 " runtime_us=%llu stack_bytes=%u priority=%u", now,
                 static_cast<unsigned>(task.xTaskNumber), task.pcTaskName,
                 static_cast<int>(task.xCoreID),
                 static_cast<unsigned long long>(task.ulRunTimeCounter),
                 static_cast<unsigned>(task.usStackHighWaterMark),
                 static_cast<unsigned>(task.uxCurrentPriority));
    }
}
}  // namespace usage_panel::amoled
#endif
