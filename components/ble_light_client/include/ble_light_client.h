#ifndef HYDRA_BLE_LIGHT_CLIENT_H
#define HYDRA_BLE_LIGHT_CLIENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "command_queue.h"
#include "light_registry.h"

typedef enum {
    BLC_STATE_DISABLED        = 0,
    BLC_STATE_IDLE            = 1,
    BLC_STATE_SCANNING        = 2,
    BLC_STATE_CONNECTING      = 3,
    BLC_STATE_DISCOVERING     = 4,
    BLC_STATE_SUBSCRIBING     = 5,
    BLC_STATE_VERIFYING       = 6,
    BLC_STATE_READY           = 7,
    BLC_STATE_WRITING         = 8,
    BLC_STATE_WAITING_CONFIRM = 9,
    BLC_STATE_COOLDOWN        = 10,
    BLC_STATE_DISCONNECTING   = 11,
    BLC_STATE_ERROR           = 12,
    BLC_STATE_BACKOFF         = 13,
} blc_state_t;

typedef struct {
    char        light_id[LIGHT_ID_LEN];
    char        command_id[CMD_ID_LEN];
    bool        success;
    int         status;
    char        message[64];
} blc_result_t;

typedef void (*blc_result_cb_t)(const blc_result_t *res, void *user);

#ifdef ESP_PLATFORM
#include "esp_err.h"
esp_err_t ble_light_client_init(void);
esp_err_t ble_light_client_start(void);
void ble_light_client_set_result_cb(blc_result_cb_t cb, void *user);
blc_state_t ble_light_client_current_state(void);
#endif

#endif /* HYDRA_BLE_LIGHT_CLIENT_H */
