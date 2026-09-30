#pragma once
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
namespace usage_panel::rlcd {
/**
 * Alert tone through the ES8311 codec and the speaker amplifier.
 *
 * Only the DAC path is configured: I2S0 standard mode, 16 kHz, 16-bit stereo, MCLK = 256 fs,
 * codec in slave mode. Playback runs on a dedicated low-priority task so the UI loop never
 * waits for audio.
 */
class Beeper {
  public:
    /**
     * Configures the codec, I2S channel and playback task. `bus` must remain valid for this
     * object's lifetime. Returns false on codec, I2S or task initialization failure.
     */
    bool begin(i2c_master_bus_handle_t bus);
    /** Queues one alert pattern; requests during playback are coalesced. No effect unless ready. */
    void alert();
    bool ready() const { return task_ != nullptr; }

  private:
    i2c_master_dev_handle_t codec_ = nullptr;
    i2s_chan_handle_t tx_ = nullptr;
    TaskHandle_t task_ = nullptr;
    bool write(uint8_t reg, uint8_t value);
    static void run(void *self);
    void play();
};
} // namespace usage_panel::rlcd
