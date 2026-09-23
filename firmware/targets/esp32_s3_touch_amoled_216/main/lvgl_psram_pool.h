#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Allocates LVGL's configured global arena from external PSRAM.
 *
 * This is an initialization invariant rather than a general allocator: the
 * requested size must exactly match CONFIG_LV_MEM_SIZE_KILOBYTES and the
 * returned block must reside in external RAM. A mismatch or allocation-domain
 * failure aborts startup so LVGL cannot silently consume scarce internal RAM.
 */
void *lvgl_psram_pool_alloc(size_t bytes);

#ifdef __cplusplus
}
#endif
