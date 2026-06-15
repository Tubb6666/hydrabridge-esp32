#ifndef HYDRA_WIFI_STATION_H
#define HYDRA_WIFI_STATION_H

#ifdef ESP_PLATFORM
#include <stdbool.h>
#include "esp_err.h"

typedef enum {
    HYDRA_WIFI_MODE_OFF = 0,
    HYDRA_WIFI_MODE_STA = 1,
    HYDRA_WIFI_MODE_AP = 2,
    HYDRA_WIFI_MODE_APSTA = 3,
} hydra_wifi_mode_t;

/* WiFi station + setup AP + mDNS responder. Reads persisted WiFi config
 * from config_store. If no station credentials are saved, or if station
 * retries fail and AP fallback is enabled, the setup AP is available at
 * http://192.168.1.10/. */
esp_err_t hydra_wifi_start(void);
esp_err_t hydra_wifi_reconfigure(void);
bool      hydra_wifi_is_connected(void);
bool      hydra_wifi_ap_is_active(void);
hydra_wifi_mode_t hydra_wifi_mode(void);

/* Pause/resume the WiFi station association without tearing down the
 * netif. Use around BLE central operations on ESP32-S3 where the shared
 * 2.4 GHz radio cannot service both reliably. */
esp_err_t hydra_wifi_pause(void);
esp_err_t hydra_wifi_resume(void);
#endif

#endif /* HYDRA_WIFI_STATION_H */
