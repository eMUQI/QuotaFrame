#pragma once

#include <cstddef>
#include <cstdint>

namespace usage_panel::amoled {

/** Memory/transfer policy shared by display initialization and compile-time checks. */
struct DisplayBufferPlan {
    uint16_t buffer_height;
    size_t transfer_bytes;
    size_t total_draw_buffer_bytes;
    bool use_psram;
    bool require_double_buffer;
};

/**
 * Returns the SPI display buffer plan.
 *
 * The draw buffer intentionally stays in internal DMA-capable memory. A 20-row
 * RGB565 buffer is 19,200 bytes, below the 32 KiB transfer budget, and a second
 * draw buffer is not required for the current non-tear-avoidance path.
 */
constexpr DisplayBufferPlan make_display_buffer_plan()
{
    constexpr uint16_t width = 480;
    constexpr uint16_t buffer_height = 20;
    constexpr uint16_t bytes_per_pixel = 2;
    constexpr uint8_t buffer_count = 1;
    constexpr size_t transfer_bytes =
        width * buffer_height * bytes_per_pixel;

    return {
        .buffer_height = buffer_height,
        .transfer_bytes = transfer_bytes,
        .total_draw_buffer_bytes = transfer_bytes * buffer_count,
        .use_psram = false,
        .require_double_buffer = false,
    };
}

/** Checks the assumptions required by the current SPI DMA transport. */
constexpr bool display_buffer_plan_is_dma_safe(const DisplayBufferPlan& plan)
{
    constexpr size_t max_internal_transfer_bytes = 32 * 1024;
    return plan.buffer_height > 0 &&
           (plan.buffer_height % 2) == 0 &&
           plan.transfer_bytes <= max_internal_transfer_bytes &&
           !plan.use_psram &&
           !plan.require_double_buffer;
}

}  // namespace usage_panel::amoled
