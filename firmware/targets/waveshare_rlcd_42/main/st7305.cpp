// Controller initialization and pixel layout adapted from Waveshare's factory driver.
// Copyright 2026 Waveshare. Licensed under Apache-2.0; see ../UPSTREAM.md.
#include "st7305.hpp"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <cstring>
namespace usage_panel::rlcd {
namespace {
constexpr gpio_num_t kMosi = GPIO_NUM_12, kSclk = GPIO_NUM_11, kDc = GPIO_NUM_5, kCs = GPIO_NUM_40,
                     kReset = GPIO_NUM_41, kTe = GPIO_NUM_6;
constexpr spi_host_device_t kHost = SPI3_HOST;
// The vendor example drives the panel at 10 MHz; a full frame then takes about 12 ms.
constexpr int kClockHz = 10 * 1000 * 1000;
// The panel scans at about 16 Hz (0xD8 OSCSW = 111, 0xB2 HFRA = 0), sweeping the gate lines
// along the landscape x axis in about 62 ms. A write that starts mid-scan leaves a vertical seam
// between old and new content for one scan; a write started at the TE rising edge (start of
// vertical blanking) stays ahead of the scan. The wait is bounded so a silent TE line only
// reintroduces the seam.
constexpr TickType_t kTeTimeout = pdMS_TO_TICKS(100);
// Column and row address windows covering the whole panel in controller units.
constexpr uint8_t kColumns[] = {0x12, 0x2a}, kRows[] = {0x00, 0xc7};

struct InitCommand {
    uint8_t cmd;
    uint8_t size;
    uint8_t data[10];
    uint16_t delay_ms;
};
// Initialization sequence from the Waveshare ESP32-S3-RLCD-4.2 factory program (display_bsp.cpp).
constexpr InitCommand kInit[] = {
    {0xd6, 2, {0x17, 0x02}, 0},                   // NVM load control
    {0xd1, 1, {0x01}, 0},                         // Booster enable
    {0xc0, 2, {0x11, 0x04}, 0},                   // Gate voltage
    {0xc1, 4, {0x41, 0x41, 0x41, 0x41}, 0},       // VSHP
    {0xc2, 4, {0x19, 0x19, 0x19, 0x19}, 0},       // VSLP
    {0xc4, 4, {0x41, 0x41, 0x41, 0x41}, 0},       // VSHN
    {0xc5, 4, {0x19, 0x19, 0x19, 0x19}, 0},       // VSLN
    {0xd8, 2, {0xa6, 0xe9}, 0},                   // OSC
    {0xb2, 1, {0x05}, 0},                         // Frame rate
    {0xb3, 10, {0xe5, 0xf6, 0x05, 0x46, 0x77, 0x77, 0x77, 0x77, 0x76, 0x45}, 0},
    {0xb4, 8, {0x05, 0x46, 0x77, 0x77, 0x77, 0x77, 0x76, 0x45}, 0},
    {0x62, 3, {0x32, 0x03, 0x1f}, 0},             // Gate timing
    {0xb7, 1, {0x13}, 0},                         // Source EQ
    {0xb0, 1, {0x64}, 0},                         // Gate line setting
    {0x11, 0, {}, 200},                           // Sleep out
    {0xc9, 1, {0x00}, 0},                         // Source voltage select
    {0x36, 1, {0x48}, 0},                         // Memory data access control
    {0x3a, 1, {0x11}, 0},                         // Data format
    {0xb9, 1, {0x20}, 0},                         // Gamma mode
    {0xb8, 1, {0x29}, 0},                         // Panel setting
    {0x21, 0, {}, 0},                             // Display inversion on
    {0x2a, 2, {0x12, 0x2a}, 0},
    {0x2b, 2, {0x00, 0xc7}, 0},
    {0x35, 1, {0x00}, 0},                         // Tearing effect line on
    {0xd0, 1, {0xff}, 0},                         // Auto power down
    {0x38, 0, {}, 0},                             // High power mode
    {0x29, 0, {}, 0},                             // Display on
};

/**
 * Converts the canvas (native landscape, row-major, 1 = black) into controller order.
 * Each controller byte holds a 2 x 4 block: two columns by four rows counted from the bottom
 * edge, with bit 7 at the block's bottom-left pixel. Controller bit value 1 is white.
 */
void pack(const uint8_t *canvas, uint8_t *out) {
    constexpr int w = Canvas::kNativeWidth, h = Canvas::kNativeHeight;
    memset(out, 0, St7305::kFrameBytes);
    for (int y = 0; y < h; ++y) {
        const int inverse = h - 1 - y;
        const int block = inverse >> 2, row = inverse & 3;
        for (int x = 0; x < w; ++x) {
            const int n = y * w + x;
            if (canvas[n >> 3] & (0x80 >> (n & 7)))
                continue;
            out[(x >> 1) * (h / 4) + block] |= 0x80 >> ((row << 1) | (x & 1));
        }
    }
}
} // namespace

bool St7305::on_done(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t *, void *ctx) {
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(static_cast<St7305 *>(ctx)->done_, &woken);
    return woken == pdTRUE;
}

void St7305::on_te(void *ctx) {
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(static_cast<St7305 *>(ctx)->te_, &woken);
    if (woken)
        portYIELD_FROM_ISR();
}

void St7305::command(uint8_t cmd, const uint8_t *data, size_t size) {
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(io_, cmd, data, size));
}

bool St7305::begin() {
    frame_ = static_cast<uint8_t *>(heap_caps_malloc(kFrameBytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    displayed_ = static_cast<uint8_t *>(heap_caps_malloc(kFrameBytes, MALLOC_CAP_DEFAULT));
    done_ = xSemaphoreCreateBinary();
    te_ = xSemaphoreCreateBinary();
    if (!frame_ || !displayed_ || !done_ || !te_)
        return false;
    xSemaphoreGive(done_);
    spi_bus_config_t bus{};
    bus.mosi_io_num = kMosi;
    bus.miso_io_num = -1;
    bus.sclk_io_num = kSclk;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = kFrameBytes;
    if (spi_bus_initialize(kHost, &bus, SPI_DMA_CH_AUTO) != ESP_OK)
        return false;
    esp_lcd_panel_io_spi_config_t io{};
    io.dc_gpio_num = kDc;
    io.cs_gpio_num = kCs;
    io.pclk_hz = kClockHz;
    io.lcd_cmd_bits = 8;
    io.lcd_param_bits = 8;
    io.spi_mode = 0;
    io.trans_queue_depth = 4;
    io.on_color_trans_done = on_done;
    io.user_ctx = this;
    if (esp_lcd_new_panel_io_spi(static_cast<esp_lcd_spi_bus_handle_t>(kHost), &io, &io_) !=
        ESP_OK)
        return false;
    gpio_config_t reset{};
    reset.pin_bit_mask = 1ULL << kReset;
    reset.mode = GPIO_MODE_OUTPUT;
    ESP_ERROR_CHECK(gpio_config(&reset));
    gpio_set_level(kReset, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(kReset, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(kReset, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    for (const auto &c : kInit) {
        command(c.cmd, c.size ? c.data : nullptr, c.size);
        if (c.delay_ms)
            vTaskDelay(pdMS_TO_TICKS(c.delay_ms));
    }
    gpio_config_t te{};
    te.pin_bit_mask = 1ULL << kTe;
    te.mode = GPIO_MODE_INPUT;
    te.intr_type = GPIO_INTR_POSEDGE;
    ESP_ERROR_CHECK(gpio_config(&te));
    const esp_err_t service = gpio_install_isr_service(0);
    if (service != ESP_OK && service != ESP_ERR_INVALID_STATE)
        return false;
    ESP_ERROR_CHECK(gpio_isr_handler_add(kTe, on_te, this));
    // Measure the startup TE period; each frame independently waits for a new TE edge.
    xSemaphoreTake(te_, 0);
    if (xSemaphoreTake(te_, kTeTimeout) == pdTRUE) {
        const int64_t start = esp_timer_get_time();
        if (xSemaphoreTake(te_, kTeTimeout) == pdTRUE)
            ESP_LOGI("rlcd42", "TE period %lld us", esp_timer_get_time() - start);
    }
    return true;
}

bool St7305::show(const Canvas &canvas) {
    // DMA owns frame_ until the completion callback releases it.
    if (xSemaphoreTake(done_, pdMS_TO_TICKS(200)) != pdTRUE) {
        ESP_LOGW("rlcd42", "Frame transfer did not complete");
        return false;
    }
    pack(canvas.data(), frame_);
    if (has_frame_ && memcmp(frame_, displayed_, kFrameBytes) == 0) {
        xSemaphoreGive(done_);
        return false;
    }
    memcpy(displayed_, frame_, kFrameBytes);
    has_frame_ = true;
    xSemaphoreTake(te_, 0); // Discard an edge from an earlier scan.
    const bool synced = xSemaphoreTake(te_, kTeTimeout) == pdTRUE;
    if (synced == te_missing_) {
        te_missing_ = !synced;
        if (te_missing_)
            ESP_LOGW("rlcd42", "No TE edge; frame sent unsynchronized");
    }
    command(0x2a, kColumns, sizeof(kColumns));
    command(0x2b, kRows, sizeof(kRows));
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_color(io_, 0x2c, frame_, kFrameBytes));
    return true;
}
} // namespace usage_panel::rlcd
