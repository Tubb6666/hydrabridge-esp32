#ifndef HYDRA_LIGHT_REGISTRY_H
#define HYDRA_LIGHT_REGISTRY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "channel_model.h"

/* light_registry
 * ==============
 * Fixed-capacity, in-memory tables of registered lights and named
 * groups. Survives reboot via NVS blob persistence on ESP-IDF.
 */

#define LIGHT_REGISTRY_CAPACITY      4
#define GROUP_REGISTRY_CAPACITY      4
#define LIGHT_GROUP_MEMBERS_MAX      LIGHT_REGISTRY_CAPACITY

#define LIGHT_ID_LEN                 33
#define LIGHT_NAME_LEN               33
#define LIGHT_SERIAL_LEN             33
#define BLE_ADDR_BYTES               6
#define GROUP_ID_LEN                 33

typedef enum {
    BLE_ADDR_PUBLIC    = 0,
    BLE_ADDR_RANDOM    = 1,
    BLE_ADDR_PUBLIC_ID = 2,
    BLE_ADDR_RANDOM_ID = 3,
} ble_addr_type_t;

typedef enum {
    LIGHT_FANOUT_SEQUENTIAL = 0,
} group_fanout_mode_t;

typedef struct {
    char            light_id[LIGHT_ID_LEN];
    char            display_name[LIGHT_NAME_LEN];
    char            serial[LIGHT_SERIAL_LEN];
    uint8_t         ble_addr[BLE_ADDR_BYTES];
    ble_addr_type_t ble_addr_type;
    uint16_t        model;
    bool            enabled;
    int8_t          last_seen_rssi;
    channel_state_t last_state;
} registered_light_t;

typedef struct {
    char                 group_id[GROUP_ID_LEN];
    char                 display_name[LIGHT_NAME_LEN];
    char                 light_ids[LIGHT_GROUP_MEMBERS_MAX][LIGHT_ID_LEN];
    uint8_t              member_count;
    bool                 enabled;
    group_fanout_mode_t  fanout_mode;
} light_group_t;

void light_registry_reset(void);

void hydra_ble_addr_from_nimble(uint8_t out[BLE_ADDR_BYTES],
                                const uint8_t nimble_val[BLE_ADDR_BYTES]);
void hydra_ble_addr_to_nimble(uint8_t out[BLE_ADDR_BYTES],
                              const uint8_t canonical[BLE_ADDR_BYTES]);
int hydra_ble_addr_format(const uint8_t addr[BLE_ADDR_BYTES],
                          char *out, size_t cap);
int hydra_ble_addr_parse(const char *text, uint8_t out[BLE_ADDR_BYTES]);

size_t light_registry_count(void);
int  light_registry_add(const registered_light_t *light);
int  light_registry_remove(const char *light_id);
const registered_light_t *light_registry_get(const char *light_id);
const registered_light_t *light_registry_get_by_serial(const char *serial);
const registered_light_t *light_registry_get_by_addr(const uint8_t *addr);
const registered_light_t *light_registry_at(size_t idx);
int light_registry_set_last_state(const char *light_id, const channel_state_t *state);
int light_registry_set_rssi(const char *light_id, int8_t rssi);
int light_registry_set_enabled(const char *light_id, bool enabled);
int light_registry_rename(const char *light_id, const char *display_name);
int light_registry_update_discovery(const char *light_id,
                                    const uint8_t addr[BLE_ADDR_BYTES],
                                    ble_addr_type_t addr_type,
                                    int8_t rssi);

size_t group_registry_count(void);
int    group_registry_add(const light_group_t *group);
int    group_registry_remove(const char *group_id);
const light_group_t *group_registry_get(const char *group_id);
const light_group_t *group_registry_at(size_t idx);

#ifdef ESP_PLATFORM
#include "esp_err.h"
esp_err_t light_registry_init(void);
esp_err_t light_registry_save(void);
#endif

#endif /* HYDRA_LIGHT_REGISTRY_H */
