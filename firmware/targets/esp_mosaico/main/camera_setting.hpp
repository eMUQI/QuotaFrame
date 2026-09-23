#pragma once

#include "esp_err.h"

namespace usage_panel::mosaico {

/** Returns the persisted camera switch; absent or invalid storage reads as off. */
bool load_camera_enabled();
/** Persists the camera switch; called by settings SAVE together with the display values. */
esp_err_t save_camera_enabled(bool enabled);

}  // namespace usage_panel::mosaico
