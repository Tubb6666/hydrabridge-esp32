#include "light_registry.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_err.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
static const char *TAG = "light_registry";

#define NS_LIGHTS  "lights"
#define NS_GROUPS  "groups"
#define KEY_BLOB   "tbl"
#define LIGHTS_BLOB_VER  1
#define GROUPS_BLOB_VER  1
#endif

static registered_light_t g_lights[LIGHT_REGISTRY_CAPACITY];
static size_t             g_light_count;

static light_group_t      g_groups[GROUP_REGISTRY_CAPACITY];
static size_t             g_group_count;

void hydra_ble_addr_from_nimble(uint8_t out[BLE_ADDR_BYTES],
                                const uint8_t nimble_val[BLE_ADDR_BYTES])
{
    if (!out || !nimble_val) return;
    for (size_t i = 0; i < BLE_ADDR_BYTES; ++i) {
        out[i] = nimble_val[BLE_ADDR_BYTES - 1 - i];
    }
}

void hydra_ble_addr_to_nimble(uint8_t out[BLE_ADDR_BYTES],
                              const uint8_t canonical[BLE_ADDR_BYTES])
{
    hydra_ble_addr_from_nimble(out, canonical);
}

int hydra_ble_addr_format(const uint8_t addr[BLE_ADDR_BYTES],
                          char *out, size_t cap)
{
    if (!addr || !out || cap < 18) return -1;
    int n = snprintf(out, cap, "%02x:%02x:%02x:%02x:%02x:%02x",
                     addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
    return (n == 17) ? 0 : -1;
}

int hydra_ble_addr_parse(const char *text, uint8_t out[BLE_ADDR_BYTES])
{
    if (!text || !out) return -1;
    unsigned int b[BLE_ADDR_BYTES];
    char tail = '\0';
    int n = sscanf(text, "%x:%x:%x:%x:%x:%x%c",
                   &b[0], &b[1], &b[2], &b[3], &b[4], &b[5], &tail);
    if (n != BLE_ADDR_BYTES) return -1;
    for (size_t i = 0; i < BLE_ADDR_BYTES; ++i) {
        if (b[i] > 0xff) return -1;
        out[i] = (uint8_t)b[i];
    }
    return 0;
}

void light_registry_reset(void)
{
    memset(g_lights, 0, sizeof g_lights);
    memset(g_groups, 0, sizeof g_groups);
    g_light_count = 0;
    g_group_count = 0;
}

size_t light_registry_count(void) { return g_light_count; }

static int find_light_idx(const char *light_id)
{
    if (!light_id) return -1;
    for (size_t i = 0; i < g_light_count; ++i) {
        if (strncmp(g_lights[i].light_id, light_id, LIGHT_ID_LEN) == 0) return (int)i;
    }
    return -1;
}

int light_registry_add(const registered_light_t *light)
{
    if (!light) return -1;
    if (g_light_count >= LIGHT_REGISTRY_CAPACITY) return -1;
    if (find_light_idx(light->light_id) >= 0) return -1;
    g_lights[g_light_count] = *light;
    g_lights[g_light_count].light_id[LIGHT_ID_LEN - 1] = '\0';
    g_lights[g_light_count].display_name[LIGHT_NAME_LEN - 1] = '\0';
    g_lights[g_light_count].serial[LIGHT_SERIAL_LEN - 1] = '\0';
    ++g_light_count;
    return 0;
}

int light_registry_remove(const char *light_id)
{
    int idx = find_light_idx(light_id);
    if (idx < 0) return -1;

    /* Remove the light from every group that references it. */
    for (size_t g = 0; g < g_group_count; ++g) {
        light_group_t *grp = &g_groups[g];
        size_t write = 0;
        for (size_t m = 0; m < grp->member_count; ++m) {
            if (strncmp(grp->light_ids[m], light_id, LIGHT_ID_LEN) != 0) {
                if (write != m) memcpy(grp->light_ids[write], grp->light_ids[m], LIGHT_ID_LEN);
                ++write;
            }
        }
        grp->member_count = (uint8_t)write;
        for (size_t m = grp->member_count; m < LIGHT_GROUP_MEMBERS_MAX; ++m) {
            grp->light_ids[m][0] = '\0';
        }
    }

    for (size_t i = (size_t)idx; i + 1 < g_light_count; ++i) {
        g_lights[i] = g_lights[i + 1];
    }
    --g_light_count;
    memset(&g_lights[g_light_count], 0, sizeof g_lights[g_light_count]);
    return 0;
}

const registered_light_t *light_registry_get(const char *light_id)
{
    int idx = find_light_idx(light_id);
    return idx < 0 ? NULL : &g_lights[idx];
}

const registered_light_t *light_registry_get_by_serial(const char *serial)
{
    if (!serial) return NULL;
    for (size_t i = 0; i < g_light_count; ++i) {
        if (strncmp(g_lights[i].serial, serial, LIGHT_SERIAL_LEN) == 0) return &g_lights[i];
    }
    return NULL;
}

const registered_light_t *light_registry_get_by_addr(const uint8_t *addr)
{
    if (!addr) return NULL;
    for (size_t i = 0; i < g_light_count; ++i) {
        if (memcmp(g_lights[i].ble_addr, addr, BLE_ADDR_BYTES) == 0) return &g_lights[i];
    }
    return NULL;
}

const registered_light_t *light_registry_at(size_t idx)
{
    if (idx >= g_light_count) return NULL;
    return &g_lights[idx];
}

int light_registry_set_last_state(const char *light_id, const channel_state_t *state)
{
    int idx = find_light_idx(light_id);
    if (idx < 0 || !state) return -1;
    g_lights[idx].last_state = *state;
    return 0;
}

int light_registry_set_rssi(const char *light_id, int8_t rssi)
{
    int idx = find_light_idx(light_id);
    if (idx < 0) return -1;
    g_lights[idx].last_seen_rssi = rssi;
    return 0;
}

int light_registry_set_enabled(const char *light_id, bool enabled)
{
    int idx = find_light_idx(light_id);
    if (idx < 0) return -1;
    g_lights[idx].enabled = enabled;
    return 0;
}

int light_registry_rename(const char *light_id, const char *display_name)
{
    int idx = find_light_idx(light_id);
    if (idx < 0 || !display_name) return -1;

    while (*display_name && isspace((unsigned char)*display_name)) {
        ++display_name;
    }
    size_t len = strnlen(display_name, LIGHT_NAME_LEN);
    while (len > 0 && isspace((unsigned char)display_name[len - 1])) {
        --len;
    }
    if (len == 0) return -1;
    if (len >= LIGHT_NAME_LEN) len = LIGHT_NAME_LEN - 1;

    memcpy(g_lights[idx].display_name, display_name, len);
    g_lights[idx].display_name[len] = '\0';
    return 0;
}

int light_registry_update_discovery(const char *light_id,
                                    const uint8_t addr[BLE_ADDR_BYTES],
                                    ble_addr_type_t addr_type,
                                    int8_t rssi)
{
    int idx = find_light_idx(light_id);
    if (idx < 0 || !addr) return -1;
    memcpy(g_lights[idx].ble_addr, addr, BLE_ADDR_BYTES);
    g_lights[idx].ble_addr_type = addr_type;
    g_lights[idx].last_seen_rssi = rssi;
    return 0;
}

size_t group_registry_count(void) { return g_group_count; }

static int find_group_idx(const char *group_id)
{
    if (!group_id) return -1;
    for (size_t i = 0; i < g_group_count; ++i) {
        if (strncmp(g_groups[i].group_id, group_id, GROUP_ID_LEN) == 0) return (int)i;
    }
    return -1;
}

int group_registry_add(const light_group_t *group)
{
    if (!group) return -1;
    if (g_group_count >= GROUP_REGISTRY_CAPACITY) return -1;
    if (find_group_idx(group->group_id) >= 0) return -1;
    if (group->member_count > LIGHT_GROUP_MEMBERS_MAX) return -1;
    g_groups[g_group_count] = *group;
    g_groups[g_group_count].group_id[GROUP_ID_LEN - 1] = '\0';
    g_groups[g_group_count].display_name[LIGHT_NAME_LEN - 1] = '\0';
    ++g_group_count;
    return 0;
}

int group_registry_remove(const char *group_id)
{
    int idx = find_group_idx(group_id);
    if (idx < 0) return -1;
    for (size_t i = (size_t)idx; i + 1 < g_group_count; ++i) {
        g_groups[i] = g_groups[i + 1];
    }
    --g_group_count;
    memset(&g_groups[g_group_count], 0, sizeof g_groups[g_group_count]);
    return 0;
}

const light_group_t *group_registry_get(const char *group_id)
{
    int idx = find_group_idx(group_id);
    return idx < 0 ? NULL : &g_groups[idx];
}

const light_group_t *group_registry_at(size_t idx)
{
    if (idx >= g_group_count) return NULL;
    return &g_groups[idx];
}

#ifdef ESP_PLATFORM

typedef struct __attribute__((packed)) {
    uint32_t schema_version;
    uint32_t count;
} blob_header_t;

static esp_err_t save_blob(const char *ns, uint32_t version,
                           const void *items, size_t item_size, size_t count)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(ns, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;

    size_t total = sizeof(blob_header_t) + count * item_size;
    uint8_t *buf = (uint8_t *)malloc(total);
    if (!buf) { nvs_close(h); return ESP_ERR_NO_MEM; }

    blob_header_t hdr = { version, (uint32_t)count };
    memcpy(buf, &hdr, sizeof hdr);
    if (count) memcpy(buf + sizeof hdr, items, count * item_size);

    err = nvs_set_blob(h, KEY_BLOB, buf, total);
    free(buf);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

static esp_err_t load_blob(const char *ns, uint32_t expected_version,
                           void *items, size_t item_size, size_t capacity,
                           size_t *count_out)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(ns, NVS_READONLY, &h);
    if (err != ESP_OK) return err;

    size_t got = 0;
    err = nvs_get_blob(h, KEY_BLOB, NULL, &got);
    if (err != ESP_OK) { nvs_close(h); return err; }
    if (got < sizeof(blob_header_t)) { nvs_close(h); return ESP_ERR_INVALID_SIZE; }

    uint8_t *buf = (uint8_t *)malloc(got);
    if (!buf) { nvs_close(h); return ESP_ERR_NO_MEM; }
    err = nvs_get_blob(h, KEY_BLOB, buf, &got);
    nvs_close(h);
    if (err != ESP_OK) { free(buf); return err; }

    blob_header_t hdr;
    memcpy(&hdr, buf, sizeof hdr);
    if (hdr.schema_version != expected_version) { free(buf); return ESP_ERR_INVALID_VERSION; }
    if (hdr.count > capacity) { free(buf); return ESP_ERR_INVALID_SIZE; }
    if (got != sizeof(blob_header_t) + hdr.count * item_size) {
        free(buf); return ESP_ERR_INVALID_SIZE;
    }

    if (hdr.count) memcpy(items, buf + sizeof hdr, hdr.count * item_size);
    *count_out = hdr.count;
    free(buf);
    return ESP_OK;
}

esp_err_t light_registry_init(void)
{
    light_registry_reset();

    size_t n = 0;
    esp_err_t err = load_blob(NS_LIGHTS, LIGHTS_BLOB_VER,
                              g_lights, sizeof g_lights[0],
                              LIGHT_REGISTRY_CAPACITY, &n);
    if (err == ESP_OK) g_light_count = n;
    else ESP_LOGW(TAG, "lights load err=0x%x; starting empty", err);

    n = 0;
    err = load_blob(NS_GROUPS, GROUPS_BLOB_VER,
                    g_groups, sizeof g_groups[0],
                    GROUP_REGISTRY_CAPACITY, &n);
    if (err == ESP_OK) g_group_count = n;
    else ESP_LOGW(TAG, "groups load err=0x%x; starting empty", err);

    ESP_LOGI(TAG, "init: %u lights, %u groups",
             (unsigned)g_light_count, (unsigned)g_group_count);
    return ESP_OK;
}

esp_err_t light_registry_save(void)
{
    esp_err_t e1 = save_blob(NS_LIGHTS, LIGHTS_BLOB_VER,
                             g_lights, sizeof g_lights[0], g_light_count);
    esp_err_t e2 = save_blob(NS_GROUPS, GROUPS_BLOB_VER,
                             g_groups, sizeof g_groups[0], g_group_count);
    return e1 != ESP_OK ? e1 : e2;
}

#endif /* ESP_PLATFORM */
