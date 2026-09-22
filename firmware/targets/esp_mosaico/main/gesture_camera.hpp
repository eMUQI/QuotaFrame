#pragma once

#include <cstddef>
#include <cstdint>

namespace usage_panel::mosaico {

/** Owns the left-slot OV3640 and its V4L2 buffers on the vision worker only. */
class GestureCamera {
public:
    bool start();
    /** Copies/converts one frame and requeues it before returning to the caller. */
    bool read_rgb(uint8_t* output, unsigned rotation, bool mirror);
    /** Returns false if resources cannot be safely released; do not restart afterward. */
    bool stop();

private:
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
