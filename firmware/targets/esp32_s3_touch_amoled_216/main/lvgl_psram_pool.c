#include "lvgl_psram_pool.h"

#include <stdint.h>
#include <stdlib.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_memory_utils.h"
#include "sdkconfig.h"

static const char *TAG = "lvgl_psram";

void *lvgl_psram_pool_alloc(size_t bytes)
{
    const size_t configured_bytes =
        (size_t)CONFIG_LV_MEM_SIZE_KILOBYTES * 1024U;
    if (bytes != configured_bytes) {
        ESP_LOGE(TAG, "unexpected LVGL pool request: requested=%u configured=%u",
                 (unsigned)bytes, (unsigned)configured_bytes);
        abort();
    }

    void *pool = heap_caps_malloc(
        bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (pool == NULL || !esp_ptr_external_ram(pool)) {
        ESP_LOGE(TAG, "external LVGL pool allocation failed: bytes=%u",
                 (unsigned)bytes);
        if (pool != NULL) {
            heap_caps_free(pool);
        }
        abort();
    }

    const uint32_t internal_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    ESP_LOGI(TAG,
             "MEMORY stage=arena_ready internal_free=%u internal_largest=%u "
             "internal_min=%u psram_free=%u arena_bytes=%u",
             (unsigned)heap_caps_get_free_size(internal_caps),
             (unsigned)heap_caps_get_largest_free_block(internal_caps),
             (unsigned)heap_caps_get_minimum_free_size(internal_caps),
             (unsigned)heap_caps_get_free_size(
                 MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
             (unsigned)bytes);
    return pool;
}
