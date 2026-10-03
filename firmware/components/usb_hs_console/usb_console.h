/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start the USB-OTG CDC console.
 *
 * CONFIG_USB_HS_CONSOLE_USB_CDC_AUTO_INIT enables this automatically before
 * app_main(). Call it manually only when automatic initialization is disabled.
 * On success, stdin, stdout, stderr and subsequent ESP_LOG output use TinyUSB
 * CDC-ACM interface 0. Calls must be serialized; a successful initialization
 * makes subsequent calls return ESP_OK without reinstalling the driver.
 * Initialization and state queries must not run concurrently.
 *
 * When CONFIG_USB_HS_CONSOLE_USB_CDC_AUTO_DOWNLOAD is enabled, the same interface also
 * emulates the USB-Serial/JTAG DTR/RTS reset behavior and uses its VID/PID, so
 * esptool can select the matching reset strategy from the USB identifier.
 * The ROM port may re-enumerate under a different device name.
 *
 * @return ESP_OK if initialization succeeds or the console is already initialized;
 * otherwise, the error from timer setup, shutdown-handler registration or TinyUSB
 * initialization.
 */
esp_err_t bsp_usb_console_init(void);

/**
 * @brief Query initialization state without probing the USB host connection.
 * Calls must not overlap bsp_usb_console_init().
 * @return true after successful initialization; false before it or after failure.
 */
bool bsp_usb_console_is_initialized(void);

#ifdef __cplusplus
}
#endif
