#include "display_transport.hpp"
#include "unity.h"

using namespace usage_panel::amoled;

TEST_CASE("display buffers stay DMA-safe without PSRAM bounce copies",
          "[amoled_display]")
{
    const DisplayBufferPlan plan = make_display_buffer_plan();

    TEST_ASSERT_FALSE(plan.use_psram);
    TEST_ASSERT_FALSE(plan.require_double_buffer);
    TEST_ASSERT_EQUAL_UINT16(20, plan.buffer_height);
    TEST_ASSERT_EQUAL_UINT32(19200, plan.transfer_bytes);
    TEST_ASSERT_EQUAL_UINT32(19200, plan.total_draw_buffer_bytes);
    TEST_ASSERT_TRUE(display_buffer_plan_is_dma_safe(plan));
}

TEST_CASE("the BSP default PSRAM buffer plan is rejected",
          "[amoled_display]")
{
    const DisplayBufferPlan bsp_default{
        .buffer_height = 50,
        .transfer_bytes = 48000,
        .total_draw_buffer_bytes = 96000,
        .use_psram = true,
        .require_double_buffer = true,
    };

    TEST_ASSERT_FALSE(display_buffer_plan_is_dma_safe(bsp_default));
}
