#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <stdbool.h>
#include "esp_err.h"

/**
 * @brief Initialize WiFi in STA + AP mode.
 * Loads credentials from NVS if available and attempts to connect in background.
 */
esp_err_t wifi_manager_init(void);

/**
 * @brief Start WiFi scanning.
 * This is non-blocking and will trigger events.
 */
esp_err_t wifi_manager_scan_start(void);

/**
 * @brief Connect to a specific WiFi network.
 * Credentials will be saved to NVS.
 */
esp_err_t wifi_manager_connect(const char *ssid, const char *password);

/**
 * @brief Return current connection status.
 */
bool wifi_manager_is_connected(void);

/**
 * @brief Get the SSID of the currently connected STA network.
 */
esp_err_t wifi_manager_get_sta_ssid(char *ssid, size_t max_len);

/**
 * @brief Enable or disable the WiFi Access Point (AP) mode.
 */
esp_err_t wifi_manager_set_ap_enabled(bool enabled);

/**
 * @brief Check if the WiFi Access Point (AP) is currently enabled.
 */
bool wifi_manager_is_ap_enabled(void);

#endif // WIFI_MANAGER_H
