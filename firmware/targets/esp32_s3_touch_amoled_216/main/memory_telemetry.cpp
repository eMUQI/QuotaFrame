#include "memory_telemetry.hpp"

#include <cstdint>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_memory_utils.h"
#include "lvgl.h"

namespace usage_panel::amoled {
namespace {
constexpr char TAG[] = "memory_domains";
constexpr uint32_t INTERNAL_CAPS = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
constexpr uint32_t EXTERNAL_CAPS = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
}  // namespace

void log_memory_checkpoint(const char* stage, bool lvgl_ready)
{
    ESP_LOGI(TAG,
             "MEMORY stage=%s internal_free=%u internal_largest=%u "
             "internal_min=%u psram_free=%u",
             stage,
             static_cast<unsigned>(heap_caps_get_free_size(INTERNAL_CAPS)),
             static_cast<unsigned>(
                 heap_caps_get_largest_free_block(INTERNAL_CAPS)),
             static_cast<unsigned>(
                 heap_caps_get_minimum_free_size(INTERNAL_CAPS)),
             static_cast<unsigned>(heap_caps_get_free_size(EXTERNAL_CAPS)));

    if (lvgl_ready) {
        lv_mem_monitor_t monitor{};
        lv_mem_monitor(&monitor);
        ESP_LOGI(TAG,
                 "LVGL_MEMORY stage=%s total=%u free=%u largest=%u "
                 "max_used=%u used_pct=%u frag_pct=%u",
                 stage,
                 static_cast<unsigned>(monitor.total_size),
                 static_cast<unsigned>(monitor.free_size),
                 static_cast<unsigned>(monitor.free_biggest_size),
                 static_cast<unsigned>(monitor.max_used),
                 static_cast<unsigned>(monitor.used_pct),
                 static_cast<unsigned>(monitor.frag_pct));
    }
}

bool verify_lvgl_allocations_external()
{
    void* probe = lv_malloc(64);
    const bool external = probe != nullptr && esp_ptr_external_ram(probe);
    ESP_LOGI(TAG, "MEMORY lvgl_probe_bytes=64 external=%s",
             external ? "yes" : "no");
    if (probe != nullptr) {
        lv_free(probe);
    }
    if (!external) {
        ESP_LOGE(TAG, "LVGL allocator did not return external RAM");
    }
    return external;
}

}  // namespace usage_panel::amoled
