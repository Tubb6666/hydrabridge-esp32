#ifndef HYDRA_WIFI_STATION_H
#define HYDRA_WIFI_STATION_H

#ifdef ESP_PLATFORM
#include <stdbool.h>
#include "esp_err.h"

/* WiFi station + mDNS responder. Reads compile-time SSID/PSK from
 * main/wifi_credentials.h (user creates from .example before flash).
 * Reconnects on disconnect with a 5s backoff. Advertises hostname
 * `hydra-ctrl` over mDNS so the web UI is reachable at
 * http://hydra-ctrl.local/ on networks that resolve .local addresses.
 * Idempotent: subsequent _start calls are no-ops. */
esp_err_t hydra_wifi_start(void);
bool      hydra_wifi_is_connected(void);

/* Pause/resume the WiFi station association without tearing down the
 * netif. Use around BLE central operations on ESP32-S3 where the shared
 * 2.4 GHz radio cannot service both reliably. */
esp_err_t hydra_wifi_pause(void);
esp_err_t hydra_wifi_resume(void);
#endif

#endif /* HYDRA_WIFI_STATION_H */
