// SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
// SPDX-License-Identifier: Apache-2.0
#include "gesture_camera.hpp"
#include "gesture_image.hpp"

#include <cerrno>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "bsp/esp_mosaico.h"
#include "esp_log.h"
#include "esp_video_device.h"
#include "esp_video_init.h"
#include "esp_video_ioctl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/usb_serial_jtag_ll.h"
#include "linux/videodev2.h"

namespace usage_panel::mosaico {
namespace {
constexpr char TAG[] = "gesture_camera";
constexpr gpio_num_t kPowerDown = GPIO_NUM_48;
constexpr gpio_num_t kFlash = GPIO_NUM_34;
// Start-up timing follows esp-vision boards/ESP32_S31_MOSAICO/camera.c.
constexpr uint32_t kPowerStabilizationMs = 20;
constexpr suseconds_t kFrameTimeoutUs = 500000;
constexpr unsigned kPrimeAttempts = 3;
// esp-mosaico-bsp mosaico_module_camera and its module manager access the
// subboard I2C1 bus, which has only internal pull-ups, at 100 kHz.
constexpr uint32_t kSccbFrequencyHz = 100000;
constexpr uint16_t kModuleEepromAddress = 0x50;
constexpr uint16_t kOv3640Address = 0x3c;
constexpr uint16_t kSc101iotAddress = 0x68;
constexpr int kProbeTimeoutMs = 50;

bool checked_ioctl(int fd, unsigned long command, void* arg)
{
    if (ioctl(fd, command, arg) == 0) return true;
    ESP_LOGW(TAG, "video ioctl 0x%lx failed: errno=%d", command, errno);
    return false;
}
}

bool GestureCamera::fail(const char* step, int code)
{
    failure_ = step;
    failure_code_ = code;
    return false;
}

bool GestureCamera::start()
{
    failure_ = nullptr;
    failure_code_ = 0;
    if (pins_claimed_ || fd_ >= 0) return fail("start while resources are held");
    if (const esp_err_t error = bsp_subboard_init(); error != ESP_OK) return fail("subboard init", error);
    // D2 shares GPIO33 with Serial/JTAG; the Type-C USB OTG console uses separate pins.
    restore_usb_pad_ = usb_serial_jtag_ll_phy_is_pad_enabled();
    restore_usb_clock_ = usb_serial_jtag_ll_module_is_enabled();
    restore_usb_interrupts_ = restore_usb_clock_ ? usb_serial_jtag_ll_get_intr_ena_status() : 0;
    if (restore_usb_clock_) usb_serial_jtag_ll_disable_intr_mask(USB_SERIAL_JTAG_LL_INTR_MASK);
    usb_serial_jtag_ll_phy_enable_pad(false);
    usb_serial_jtag_ll_enable_bus_clock(false);
    pins_claimed_ = true;
    gpio_reset_pin(GPIO_NUM_14);
    gpio_set_direction(GPIO_NUM_14, GPIO_MODE_INPUT);
    // The illuminator is active-low and must never be driven by gesture capture.
    gpio_set_level(kFlash, 1);
    gpio_set_direction(kFlash, GPIO_MODE_OUTPUT);
    // The CameraBoard oscillator and sensor need time after the slot is claimed
    // before the first SCCB access in esp_video_init.
    vTaskDelay(pdMS_TO_TICKS(kPowerStabilizationMs));
    // Diagnostic only: the sensor may still be held in power-down before
    // esp_video_init drives PWDN, so a missing sensor ACK here is not an error.
    i2c_master_bus_handle_t sccb = bsp_subboard_get_i2c_bus();
    const auto ack = [&](uint16_t address) {
        return i2c_master_probe(sccb, address, kProbeTimeoutMs) == ESP_OK ? "ack" : "no ack";
    };
    const bool eeprom_ack = i2c_master_probe(sccb, kModuleEepromAddress, kProbeTimeoutMs) == ESP_OK;
    ESP_LOGI(TAG, "subboard I2C probe: module EEPROM 0x50 %s, OV3640 0x3c %s, SC101IOT 0x68 %s",
             eeprom_ack ? "ack" : "no ack", ack(kOv3640Address), ack(kSc101iotAddress));

    esp_video_init_dvp_config_t dvp{};
    dvp.sccb_config.init_sccb = false;
    dvp.sccb_config.i2c_handle = sccb;
    dvp.sccb_config.freq = kSccbFrequencyHz;
    dvp.reset_pin = GPIO_NUM_53;
    dvp.pwdn_pin = kPowerDown;
    dvp.dvp_pin.data_width = CAM_CTLR_DATA_WIDTH_8;
    const gpio_num_t data[] = {GPIO_NUM_16, GPIO_NUM_15, GPIO_NUM_33, GPIO_NUM_4,
                              GPIO_NUM_14, GPIO_NUM_12, GPIO_NUM_18, GPIO_NUM_13};
    for (unsigned i = 0; i < 8; ++i) dvp.dvp_pin.data_io[i] = data[i];
    dvp.dvp_pin.vsync_io = GPIO_NUM_55;
    dvp.dvp_pin.de_io = GPIO_NUM_19;
    dvp.dvp_pin.pclk_io = GPIO_NUM_17;
    dvp.dvp_pin.xclk_io = GPIO_NUM_NC;
    dvp.xclk_freq = 0;  // CameraBoard supplies its own 24 MHz oscillator.
    esp_video_init_config_t config{};
    config.dvp = &dvp;
    const esp_err_t error = esp_video_init_with_flags(&config, ESP_VIDEO_INIT_FLAGS_DVP);
    if (error != ESP_OK) {
        ESP_LOGW(TAG, "left-slot camera unavailable: %s", esp_err_to_name(error));
        return fail("esp_video_init (sensor not detected?)", error);
    }
    video_initialized_ = true;
    fd_ = open(ESP_VIDEO_DVP_DEVICE_NAME, O_RDWR);
    // esp_video_init succeeds without registering the device when sensor
    // detection fails; code reports whether the module EEPROM answered.
    if (fd_ < 0 && errno == ENOENT)
        return fail("no sensor detected (OV3640 0x3c / SC101IOT 0x68)", eeprom_ack ? 1 : 0);
    if (fd_ < 0) return fail("open video device", errno);
    v4l2_format format{};
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    // Preserve the detected sensor's configured field of view, as in mosaico_module_camera.
    if (!checked_ioctl(fd_, VIDIOC_G_FMT, &format)) return fail("get format", errno);
    const unsigned requested_width = format.fmt.pix.width;
    const unsigned requested_height = format.fmt.pix.height;
    if (!((requested_width == 1280 && requested_height == 720) ||
          (requested_width == 640 && requested_height == 480))) return fail("unsupported frame size");
    format.fmt.pix.pixelformat = V4L2_PIX_FMT_UYVY;
    if (!checked_ioctl(fd_, VIDIOC_S_FMT, &format)) return fail("set format", errno);
    if (format.fmt.pix.pixelformat != V4L2_PIX_FMT_UYVY ||
        format.fmt.pix.width != requested_width || format.fmt.pix.height != requested_height) return fail("format mismatch");
    width_ = format.fmt.pix.width;
    height_ = format.fmt.pix.height;
    stride_ = format.fmt.pix.bytesperline;
    if (!stride_) stride_ = width_ * 2;
    timeval timeout{0, kFrameTimeoutUs};
    if (!checked_ioctl(fd_, VIDIOC_S_DQBUF_TIMEOUT, &timeout)) return fail("set dequeue timeout", errno);
    v4l2_requestbuffers request{};
    request.count = 2;
    request.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    request.memory = V4L2_MEMORY_MMAP;
    if (!checked_ioctl(fd_, VIDIOC_REQBUFS, &request) || request.count != 2)
        return fail("request buffers", errno);
    for (unsigned i = 0; i < 2; ++i) {
        v4l2_buffer buffer{};
        buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buffer.memory = V4L2_MEMORY_MMAP;
        buffer.index = i;
        if (!checked_ioctl(fd_, VIDIOC_QUERYBUF, &buffer)) return fail("query buffer", errno);
        buffers_[i] = mmap(nullptr, buffer.length, PROT_READ | PROT_WRITE,
                           MAP_SHARED, fd_, buffer.m.offset);
        if (buffers_[i] == MAP_FAILED) { buffers_[i] = nullptr; return fail("map buffer", errno); }
        lengths_[i] = buffer.length;
        if (!checked_ioctl(fd_, VIDIOC_QBUF, &buffer)) return fail("queue buffer", errno);
    }
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    streaming_ = checked_ioctl(fd_, VIDIOC_STREAMON, &type);
    if (!streaming_) return fail("stream on", errno);
    // The first frames arrive only after sensor start-up. Discard one per
    // buffer, allowing a few frame timeouts before reporting a dead stream.
    for (unsigned primed = 0, attempts = 0; primed < 2;) {
        v4l2_buffer buffer{};
        buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buffer.memory = V4L2_MEMORY_MMAP;
        if (ioctl(fd_, VIDIOC_DQBUF, &buffer) != 0) {
            if (++attempts >= kPrimeAttempts) return fail("prime stream (no frames)", errno);
            continue;
        }
        if (!checked_ioctl(fd_, VIDIOC_QBUF, &buffer)) return fail("prime stream requeue", errno);
        ++primed;
    }
    ESP_LOGI(TAG, "camera UYVY %ux%u stride=%u buffers=2", width_, height_, stride_);
    return true;
}

bool GestureCamera::read_rgb(uint8_t* output, unsigned rotation, bool mirror)
{
    v4l2_buffer buffer{};
    buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buffer.memory = V4L2_MEMORY_MMAP;
    if (!streaming_) return fail("read while stopped");
    if (!checked_ioctl(fd_, VIDIOC_DQBUF, &buffer)) return fail("dequeue frame (no frames?)", errno);
    // At most two capture buffers exist. Prefer the newer completed frame if
    // inference left both queued; never accumulate camera latency across cycles.
    timeval nonblocking{};
    bool timeout_ok = checked_ioctl(fd_, VIDIOC_S_DQBUF_TIMEOUT, &nonblocking);
    if (timeout_ok) {
        v4l2_buffer newer{};
        newer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        newer.memory = V4L2_MEMORY_MMAP;
        if (ioctl(fd_, VIDIOC_DQBUF, &newer) == 0) {
            const bool returned = checked_ioctl(fd_, VIDIOC_QBUF, &buffer);
            buffer = newer;
            timeout_ok = returned;
        }
        timeval blocking{0, kFrameTimeoutUs};
        timeout_ok = checked_ioctl(fd_, VIDIOC_S_DQBUF_TIMEOUT, &blocking) && timeout_ok;
    }
    bool converted = false;
    if (buffer.index < 2 && buffers_[buffer.index] && !(buffer.flags & V4L2_BUF_FLAG_ERROR)) {
        const size_t bytes = buffer.bytesused ? buffer.bytesused : lengths_[buffer.index];
        if (bytes <= lengths_[buffer.index])
            converted = prepare_gesture_image(static_cast<const uint8_t*>(buffers_[buffer.index]),
                bytes, width_, height_, stride_, output, rotation, mirror);
    }
    const bool returned = checked_ioctl(fd_, VIDIOC_QBUF, &buffer);
    if (!converted) return fail("convert frame", int(buffer.bytesused));
    if (!returned || !timeout_ok) return fail("requeue frame", errno);
    return true;
}

bool GestureCamera::stop()
{
    if (streaming_) {
        int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (!checked_ioctl(fd_, VIDIOC_STREAMOFF, &type)) return fail("stream off", errno);
        streaming_ = false;
    }
    if (fd_ >= 0) {
        for (unsigned i = 0; i < 2; ++i) {
            if (buffers_[i]) {
                if (munmap(buffers_[i], lengths_[i]) != 0) return fail("unmap buffer", errno);
                buffers_[i] = nullptr;
            }
        }
        if (close(fd_) != 0) return fail("close video device", errno);
        fd_ = -1;
    }
    if (video_initialized_) {
        if (const esp_err_t error = esp_video_deinit_with_flags(ESP_VIDEO_INIT_FLAGS_DVP); error != ESP_OK)
            return fail("esp_video_deinit", error);
        video_initialized_ = false;
    }
    if (pins_claimed_) {
        gpio_set_level(kPowerDown, 1);
        gpio_set_direction(kPowerDown, GPIO_MODE_OUTPUT);
        gpio_set_level(kFlash, 1);
        // DVP is stopped before restoring the shared pads and EEPROM address line.
        gpio_reset_pin(GPIO_NUM_33);
        if (restore_usb_clock_) usb_serial_jtag_ll_enable_bus_clock(true);
        usb_serial_jtag_ll_phy_enable_pad(restore_usb_pad_);
        if (restore_usb_clock_ && restore_usb_interrupts_)
            usb_serial_jtag_ll_ena_intr_mask(restore_usb_interrupts_);
        if (const esp_err_t error = bsp_subboard_apply_address_select(BSP_SUBBOARD_SLOT_LEFT); error != ESP_OK)
            return fail("restore address select", error);
        pins_claimed_ = false;
    }
    return true;
}

}  // namespace usage_panel::mosaico
