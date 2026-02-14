#pragma once

#include "esp_err.h"

/**
 * @brief Connect to hardcoded WiFi network.
 * Called after app_wifi_init() to inject credentials
 * since SenseCraft provisioning was removed.
 */
esp_err_t debi_wifi_connect(void);

