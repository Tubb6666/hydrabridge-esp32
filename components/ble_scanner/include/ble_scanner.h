#ifndef HYDRA_BLE_SCANNER_H
#define HYDRA_BLE_SCANNER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "light_registry.h"

#define HYDRA_MODEL_HYDRA_64HD       335
#define BLE_SCANNER_NAME_MAX          32
#define BLE_SCANNER_SERIAL_MAX        33

typedef struct {
    uint8_t  version;
    uint8_t  flags;
    uint16_t model;
    char     serial[BLE_SCANNER_SERIAL_MAX];
    bool     parsed_ok;
} myai_manuf_t;

typedef struct {
    uint8_t          ble_addr[BLE_ADDR_BYTES];
    ble_addr_type_t  ble_addr_type;
    int8_t           rssi;
    char             name[BLE_SCANNER_NAME_MAX];
    myai_manuf_t     manuf;
    bool             has_hydra_service;  /* set if 128-bit service UUID 01FF0100-... seen in adv (for pairing mode discovery) */
    bool             has_manuf_data;     /* set if any 0xFF manufacturer specific data was present (even if parse failed) - helps pairing mode */
} ble_scan_result_t;

int ble_scanner_parse_manuf(const uint8_t *data, size_t len, myai_manuf_t *out);
bool ble_scanner_is_ai_model(uint16_t model);

#ifdef ESP_PLATFORM
#include "esp_err.h"
typedef void (*ble_scanner_callback_t)(const ble_scan_result_t *result, void *user_ctx);
esp_err_t ble_scanner_init(void);
esp_err_t ble_scanner_start(ble_scanner_callback_t cb, void *user_ctx, uint32_t duration_ms);
esp_err_t ble_scanner_stop(void);
#endif

#endif /* HYDRA_BLE_SCANNER_H */
