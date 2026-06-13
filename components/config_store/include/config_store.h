#ifndef HYDRA_CONFIG_STORE_H
#define HYDRA_CONFIG_STORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* config_store
 * ============
 * Persistent configuration for the four always-present categories:
 *   - controller (id, naming, BLE knobs)
 *   - modbus     (RS485 / Modbus RTU slave parameters)
 *   - wifi       (station SSID/PSK, AP fallback)
 *   - mqtt       (broker + topics + Home Assistant discovery)
 *
 * Each record lives in its own NVS namespace as a single blob:
 *   [uint32_t schema_version][packed struct bytes]
 *
 * Loads fall back to defaults_X() on any of:
 *   - namespace missing
 *   - blob key missing
 *   - schema version mismatch
 *   - blob size mismatch (e.g. struct grew)
 *
 * Defaults are exposed as pure-C functions so they can be tested on the
 * host without an NVS implementation. Light + group records are NOT
 * here -- they live in light_registry (Phase 3.1).
 */

#define CONFIG_SCHEMA_CONTROLLER  1
#define CONFIG_SCHEMA_MODBUS      1
#define CONFIG_SCHEMA_WIFI        1
#define CONFIG_SCHEMA_MQTT        1

/* ---- string-size budgets (include the trailing NUL) ---- */
#define CONFIG_CONTROLLER_ID_LEN   33
#define CONFIG_DEVICE_NAME_LEN     33
#define CONFIG_TIMEZONE_LEN        17
#define CONFIG_FIRMWARE_VER_LEN    17
#define CONFIG_WIFI_SSID_LEN       33  /* 802.11 max 32 + NUL */
#define CONFIG_WIFI_PSK_LEN        65  /* WPA2 max 63 + NUL  */
#define CONFIG_MQTT_HOST_LEN       65
#define CONFIG_MQTT_USER_LEN       33
#define CONFIG_MQTT_PSK_LEN        65
#define CONFIG_MQTT_TOPIC_LEN      33
#define CONFIG_MQTT_HA_PREFIX_LEN  33

typedef enum {
    MODBUS_PARITY_NONE = 0,
    MODBUS_PARITY_EVEN = 1,
    MODBUS_PARITY_ODD  = 2,
} modbus_parity_t;

typedef struct {
    char     controller_id[CONFIG_CONTROLLER_ID_LEN];
    char     device_name[CONFIG_DEVICE_NAME_LEN];
    char     timezone[CONFIG_TIMEZONE_LEN];
    char     firmware_version[CONFIG_FIRMWARE_VER_LEN];
    bool     ota_enabled;
    uint8_t  max_registered_lights;
    uint32_t ble_idle_disconnect_ms;
    uint8_t  ble_command_concurrency;
} config_controller_t;

typedef struct {
    bool     enabled;
    bool     master_mode_enabled;
    uint8_t  slave_address;
    uint32_t baud_rate;
    uint8_t  data_bits;
    modbus_parity_t parity;
    uint8_t  stop_bits;
    uint8_t  uart_port;
    int8_t   tx_pin;
    int8_t   rx_pin;
    int8_t   rts_de_pin;
    uint32_t response_timeout_ms;
    uint32_t command_watchdog_ms;
} config_modbus_t;

typedef struct {
    bool     enabled;
    char     ssid[CONFIG_WIFI_SSID_LEN];
    char     password[CONFIG_WIFI_PSK_LEN];
    bool     ap_fallback_enabled;
} config_wifi_t;

typedef struct {
    bool     enabled;
    char     host[CONFIG_MQTT_HOST_LEN];
    uint16_t port;
    char     username[CONFIG_MQTT_USER_LEN];
    char     password[CONFIG_MQTT_PSK_LEN];
    char     base_topic[CONFIG_MQTT_TOPIC_LEN];
    bool     home_assistant_discovery;
    char     home_assistant_prefix[CONFIG_MQTT_HA_PREFIX_LEN];
} config_mqtt_t;

/* ---- defaults factories (pure C, host-testable) ---- */
void config_defaults_controller(config_controller_t *out);
void config_defaults_modbus(config_modbus_t *out);
void config_defaults_wifi(config_wifi_t *out);
void config_defaults_mqtt(config_mqtt_t *out);

/* ---- ESP-IDF NVS API (only available in the firmware build) ---- */
#ifdef ESP_PLATFORM
#include "esp_err.h"

esp_err_t config_store_init(void);

esp_err_t config_store_load_controller(config_controller_t *out);
esp_err_t config_store_save_controller(const config_controller_t *in);

esp_err_t config_store_load_modbus(config_modbus_t *out);
esp_err_t config_store_save_modbus(const config_modbus_t *in);

esp_err_t config_store_load_wifi(config_wifi_t *out);
esp_err_t config_store_save_wifi(const config_wifi_t *in);

esp_err_t config_store_load_mqtt(config_mqtt_t *out);
esp_err_t config_store_save_mqtt(const config_mqtt_t *in);

/* For tests / factory reset: erase every config namespace. */
esp_err_t config_store_factory_reset(void);
#endif /* ESP_PLATFORM */

#endif /* HYDRA_CONFIG_STORE_H */
