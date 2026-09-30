#pragma once
#include "canvas.hpp"
#include "esp_lcd_panel_io.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <cstdint>
namespace usage_panel::rlcd {
/**
 * ST7305 reflective LCD controller on the ESP32-S3-RLCD-4.2 (400 x 300, 1 bit per pixel).
 *
 * The panel has no ghosting and no refresh waveform, so every frame is a full-window write.
 * Frames are converted from the canvas layout into the controller's 2 x 4 pixel-block layout
 * and sent only when they differ from the frame already on the panel.
 */
class St7305 {
  public:
    static constexpr int kFrameBytes = Canvas::kBytes;

    /** Initializes SPI3, resets the controller and runs the panel initialization sequence. */
    bool begin();
    /**
     * Transfers `canvas` unless it matches the last submitted frame; `force` bypasses this check.
     * Waits up to 200 ms for the previous DMA transfer, then up to 100 ms for the next TE
     * edge. A missing TE edge permits an unsynchronized write; a DMA timeout skips the frame
     * without modifying the transfer buffer.
     * @return true when a transfer was started; false when unchanged or DMA is still busy.
     */
    bool show(const Canvas &canvas, bool force = false);

  private:
    esp_lcd_panel_io_handle_t io_ = nullptr;
    uint8_t *frame_ = nullptr;     // DMA-capable, in controller layout.
    uint8_t *displayed_ = nullptr; // Copy of the last submitted frame.
    SemaphoreHandle_t done_ = nullptr;
    SemaphoreHandle_t te_ = nullptr; // Given on each TE rising edge.
    bool has_frame_ = false;
    bool te_missing_ = false;
    void command(uint8_t cmd, const uint8_t *data = nullptr, size_t size = 0);
    static bool on_done(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t *, void *ctx);
    static void on_te(void *ctx);
};
} // namespace usage_panel::rlcd
