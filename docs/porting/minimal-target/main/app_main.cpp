#include "esp_log.h"

namespace {
constexpr char kTag[] = "example_target";
}

extern "C" void app_main(void) {
    ESP_LOGI(kTag, "minimal target skeleton booted");

    // Replace this skeleton with the real target integration in this order:
    // 1. board/BSP and display initialization;
    // 2. target-owned UI and input routing;
    // 3. usage_core / usage_protocol presentation glue;
    // 4. BleServiceConfig and secure usage_ble startup;
    // 5. usage_ota / Folder Push integration for maintained targets;
    // 6. mark a new OTA image valid only after required target initialization succeeds.
    //
    // A generic display/input HAL is intentionally absent; each target owns
    // its display and input routing.
}
