#pragma once

#include "gesture_image.hpp"

#include <cstddef>
#include <cstdint>

namespace usage_panel::mosaico {

/** Owns the left-slot CameraBoard (OV3640 or SC101IOT) and its V4L2 buffers on the vision worker only. */
class GestureCamera {
public:
    bool start();
    /** Copies/converts one frame and requeues it before returning to the caller. */
    bool read_rgb(uint8_t* output, unsigned rotation, bool mirror);
    /** Packed RGB dimensions for the active capture format and rotation. */
    GestureImageSize image_size(unsigned rotation) const { return gesture_image_size(width_, height_, rotation); }
    /** Returns false if resources cannot be safely released; do not restart afterward. */
    bool stop();
    /** Names the step of the most recent failed call, or nullptr; code is errno or esp_err_t. */
    const char* failure() const { return failure_; }
    int failure_code() const { return failure_code_; }

private:
    bool fail(const char* step, int code = 0);

    const char* failure_ = nullptr;
    int failure_code_ = 0;
    int fd_ = -1;
    bool pins_claimed_ = false;
    bool video_initialized_ = false;
    bool streaming_ = false;
    bool restore_usb_pad_ = false;
    bool restore_usb_clock_ = false;
    uint32_t restore_usb_interrupts_ = 0;
    void* buffers_[2]{};
    size_t lengths_[2]{};
    unsigned width_ = 0;
    unsigned height_ = 0;
    unsigned stride_ = 0;
};

}  // namespace usage_panel::mosaico
