// Codec register configuration adapted from Espressif esp_codec_dev 1.3.5.
// Copyright 2023 Espressif Systems (Shanghai) CO LTD. Apache-2.0; see ../UPSTREAM.md.
#include "beeper.hpp"
#include "driver/gpio.h"
#include "esp_log.h"
#include <cmath>
#include <cstdint>
namespace usage_panel::rlcd {
namespace {
constexpr uint8_t kCodecAddress = 0x18;
constexpr gpio_num_t kMclk = GPIO_NUM_16, kBclk = GPIO_NUM_9, kWs = GPIO_NUM_45, kDout = GPIO_NUM_8,
                     kAmplifier = GPIO_NUM_46;
constexpr uint32_t kSampleRate = 16000;
constexpr int kToneHz = 2000;          // 8 samples per period at 16 kHz.
constexpr int kToneMs = 120, kGapMs = 80, kTones = 3;
// DAC volume register: 0xbf is 0 dB, 0.5 dB per step.
constexpr uint8_t kVolume = 0xaf;

struct Reg {
    uint8_t reg, value;
};
// ES8311 register values for DAC-only slave operation with MCLK = 4.096 MHz and fs = 16 kHz,
// derived from esp_codec_dev 1.3.5 (es8311_open, es8311_config_sample, es8311_start).
constexpr Reg kSetup[] = {
    {0x44, 0x08},               // I2C noise immunity.
    {0x01, 0x30}, {0x02, 0x00}, {0x03, 0x10}, {0x16, 0x24}, {0x04, 0x10}, {0x05, 0x00},
    {0x0b, 0x00}, {0x0c, 0x00}, {0x10, 0x1f}, {0x11, 0x7f},
    {0x00, 0x80},               // Slave mode, power on.
    {0x01, 0x3f},               // MCLK from the pin, all clocks enabled.
    {0x13, 0x10}, {0x1b, 0x0a}, {0x1c, 0x6a},
    {0x02, 0x00},               // Pre-divider 1, multiplier 1.
    {0x05, 0x00},               // ADC and DAC clock dividers 1.
    {0x03, 0x10}, {0x04, 0x20}, // Single speed; ADC and DAC oversampling.
    {0x07, 0x00}, {0x08, 0xff}, // LRCK divider 256.
    {0x06, 0x03},               // BCLK divider 4.
    {0x09, 0x0c}, {0x0a, 0x4c}, // I2S, 16 bit; DAC input on, ADC output off.
    {0x17, 0xbf}, {0x0e, 0x02}, {0x12, 0x00}, {0x14, 0x1a}, {0x0d, 0x01}, {0x15, 0x40},
    {0x37, 0x08}, {0x45, 0x00},
    {0x31, 0x00},               // DAC unmuted.
    {0x32, kVolume},
};
} // namespace

bool Beeper::write(uint8_t reg, uint8_t value) {
    const uint8_t bytes[] = {reg, value};
    return i2c_master_transmit(codec_, bytes, sizeof(bytes), 100) == ESP_OK;
}

bool Beeper::begin(i2c_master_bus_handle_t bus) {
    if (i2c_master_probe(bus, kCodecAddress, 100) != ESP_OK)
        return false;
    i2c_device_config_t device{};
    device.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    device.device_address = kCodecAddress;
    device.scl_speed_hz = 400000;
    if (i2c_master_bus_add_device(bus, &device, &codec_) != ESP_OK)
        return false;
    i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    channel.auto_clear = true;
    if (i2s_new_channel(&channel, &tx_, nullptr) != ESP_OK)
        return false;
    i2s_std_config_t std{};
    std.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(kSampleRate);
    std.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    std.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
    std.gpio_cfg.mclk = kMclk;
    std.gpio_cfg.bclk = kBclk;
    std.gpio_cfg.ws = kWs;
    std.gpio_cfg.dout = kDout;
    std.gpio_cfg.din = I2S_GPIO_UNUSED;
    if (i2s_channel_init_std_mode(tx_, &std) != ESP_OK)
        return false;
    // The codec's clock manager needs MCLK running while it is configured.
    if (i2s_channel_enable(tx_) != ESP_OK)
        return false;
    // The first transaction after codec power-up may fail; the table repeats this register.
    write(kSetup[0].reg, kSetup[0].value);
    bool ok = true;
    for (const auto &r : kSetup)
        ok = write(r.reg, r.value) && ok;
    i2s_channel_disable(tx_);
    if (!ok)
        return false;
    gpio_config_t amplifier{};
    amplifier.pin_bit_mask = 1ULL << kAmplifier;
    amplifier.mode = GPIO_MODE_OUTPUT;
    ESP_ERROR_CHECK(gpio_config(&amplifier));
    gpio_set_level(kAmplifier, 0);
    return xTaskCreate(run, "beeper", 3072, this, 2, &task_) == pdPASS;
}

void Beeper::alert() {
    if (task_)
        xTaskNotifyGive(task_);
}

void Beeper::run(void *self) {
    auto *beeper = static_cast<Beeper *>(self);
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        beeper->play();
        // Coalesce requests received during playback to prevent repeated alert patterns.
        ulTaskNotifyTake(pdTRUE, 0);
    }
}

void Beeper::play() {
    constexpr int kPeriod = kSampleRate / kToneHz;
    constexpr int kToneFrames = kSampleRate * kToneMs / 1000, kGapFrames = kSampleRate * kGapMs / 1000;
    static int16_t tone[kPeriod * 2 * 8], silence[kPeriod * 2 * 8];
    static bool built = false;
    if (!built) {
        for (int i = 0; i < kPeriod * 8; ++i) {
            const int16_t s = static_cast<int16_t>(12000 * std::sin(2 * M_PI * i / kPeriod));
            tone[i * 2] = tone[i * 2 + 1] = s;
        }
        built = true;
    }
    if (i2s_channel_enable(tx_) != ESP_OK)
        return;
    gpio_set_level(kAmplifier, 1);
    size_t written;
    for (int t = 0; t < kTones; ++t) {
        for (int f = 0; f < kToneFrames; f += kPeriod * 8)
            i2s_channel_write(tx_, tone, sizeof(tone), &written, pdMS_TO_TICKS(100));
        for (int f = 0; f < kGapFrames; f += kPeriod * 8)
            i2s_channel_write(tx_, silence, sizeof(silence), &written, pdMS_TO_TICKS(100));
    }
    gpio_set_level(kAmplifier, 0);
    i2s_channel_disable(tx_);
}
} // namespace usage_panel::rlcd
