#include "modbus_interface.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "channel_model.h"
#include "command_engine.h"
#include "command_queue.h"
#include "config_store.h"
#include "light_registry.h"
#include "preset_engine.h"

#ifdef ESP_PLATFORM
#include "esp_log.h"
static const char *TAG = "modbus_interface";
#endif

static uint16_t g_store[MODBUS_STORE_REG_COUNT];

void modbus_store_reset(void)
{
    memset(g_store, 0, sizeof g_store);
    g_store[HYDRA_MODBUS_REG_MAGIC]       = HYDRA_MODBUS_MAGIC_VALUE;
    g_store[HYDRA_MODBUS_REG_MAP_VERSION] = HYDRA_MODBUS_REG_MAP_VERSION_VALUE;
    g_store[HYDRA_MODBUS_REG_FW_VERSION_MAJOR] = 0;
    g_store[HYDRA_MODBUS_REG_FW_VERSION_MINOR] = 1;
    g_store[HYDRA_MODBUS_REG_FW_VERSION_PATCH] = 0;
}

void modbus_store_set(uint16_t addr, uint16_t value)
{
    if (addr >= MODBUS_STORE_REG_COUNT) return;
    g_store[addr] = value;
}

uint16_t modbus_store_get(uint16_t addr)
{
    if (addr >= MODBUS_STORE_REG_COUNT) return 0;
    return g_store[addr];
}

int modbus_store_read(uint16_t addr, uint16_t count, uint16_t *out)
{
    if (!out) return -1;
    if ((uint32_t)addr + count > MODBUS_STORE_REG_COUNT) return -1;
    for (uint16_t i = 0; i < count; ++i) out[i] = g_store[addr + i];
    return 0;
}

int modbus_store_write(uint16_t addr, uint16_t count, const uint16_t *values)
{
    if (!values) return -1;
    if ((uint32_t)addr + count > MODBUS_STORE_REG_COUNT) return -1;
    for (uint16_t i = 0; i < count; ++i) g_store[addr + i] = values[i];
    modbus_store_process_pending();
    return 0;
}

static uint16_t light_reg(uint8_t light_idx, uint16_t off)
{
    return g_store[HYDRA_MODBUS_LIGHT_BASE(light_idx) + off];
}

static void light_reg_set(uint8_t light_idx, uint16_t off, uint16_t v)
{
    g_store[HYDRA_MODBUS_LIGHT_BASE(light_idx) + off] = v;
}

static ce_kind_t cmd_code_to_kind(uint16_t code)
{
    switch (code) {
        case HYDRA_CMD_CODE_OFF:            return CE_KIND_POWER_OFF;
        case HYDRA_CMD_CODE_ON:             return CE_KIND_POWER_ON;
        case HYDRA_CMD_CODE_APPLY_CHANNELS: return CE_KIND_SET_CHANNELS;
        case HYDRA_CMD_CODE_PRESET:         return CE_KIND_PRESET;
        case HYDRA_CMD_CODE_RAMP:           return CE_KIND_RAMP;
        case HYDRA_CMD_CODE_IDENTIFY:       return CE_KIND_IDENTIFY;
        default:                            return CE_KIND_NONE;
    }
}

static const char *k_channel_names_in_modbus_order[CHANNEL_COUNT] = {
    "brightness", "coolwhite", "blue", "deepred",
    "violet", "uv", "green", "royalblue", "moonlight"
};

static void process_light_block(uint8_t idx)
{
    if (idx >= HYDRA_MODBUS_MAX_LIGHTS) return;

    uint16_t cmd_seq      = light_reg(idx, HYDRA_LIGHT_OFF_COMMAND_SEQ);
    uint16_t last_seq     = light_reg(idx, HYDRA_LIGHT_OFF_LAST_COMMAND_SEQ);
    uint16_t cmd_code     = light_reg(idx, HYDRA_LIGHT_OFF_COMMAND_CODE);

    if (cmd_seq == last_seq) return;
    if (cmd_code == HYDRA_CMD_CODE_NOOP) {
        light_reg_set(idx, HYDRA_LIGHT_OFF_LAST_COMMAND_SEQ, cmd_seq);
        light_reg_set(idx, HYDRA_LIGHT_OFF_LAST_RESULT_CODE, HYDRA_MODBUS_RESULT_IDLE);
        return;
    }

    const registered_light_t *light = light_registry_at(idx);
    if (!light) {
        light_reg_set(idx, HYDRA_LIGHT_OFF_LAST_COMMAND_SEQ, cmd_seq);
        light_reg_set(idx, HYDRA_LIGHT_OFF_LAST_RESULT_CODE, HYDRA_MODBUS_RESULT_INVALID_TARGET);
        return;
    }

    ce_request_t r;
    memset(&r, 0, sizeof r);
    r.source = CMD_SOURCE_MODBUS;
    strncpy(r.target_id, light->light_id, LIGHT_ID_LEN - 1);
    r.kind = cmd_code_to_kind(cmd_code);
    r.scene_timeout_sec = light_reg(idx, HYDRA_LIGHT_OFF_TIMEOUT_SECONDS);
    r.preset = (preset_id_t)light_reg(idx, HYDRA_LIGHT_OFF_PRESET_ID);
    r.replace = light_reg(idx, HYDRA_LIGHT_OFF_REPLACE_FLAG) ? true : false;

    if (r.kind == CE_KIND_SET_CHANNELS) {
        r.replace = true;
        for (size_t i = 0; i < CHANNEL_COUNT; ++i) {
            r.channels[i].name  = k_channel_names_in_modbus_order[i];
            r.channels[i].value = light_reg(idx, HYDRA_LIGHT_OFF_CH_BRIGHTNESS + i);
        }
        r.channel_count = CHANNEL_COUNT;
    }

    if (r.kind == CE_KIND_RAMP) {
        r.ramp_from = light_reg(idx, HYDRA_LIGHT_OFF_RAMP_FROM);
        r.ramp_to   = light_reg(idx, HYDRA_LIGHT_OFF_RAMP_TO);
        r.ramp_duration_ms =
            (uint32_t)light_reg(idx, HYDRA_LIGHT_OFF_RAMP_DURATION_MS_LOW) |
            ((uint32_t)light_reg(idx, HYDRA_LIGHT_OFF_RAMP_DURATION_MS_HIGH) << 16);
        r.ramp_steps = light_reg(idx, HYDRA_LIGHT_OFF_RAMP_STEPS);
    }

    char cmd_id[CMD_ID_LEN];
    ce_result_t res = command_engine_submit(&r, cmd_id);

    light_reg_set(idx, HYDRA_LIGHT_OFF_LAST_COMMAND_SEQ, cmd_seq);
    light_reg_set(idx, HYDRA_LIGHT_OFF_LAST_RESULT_CODE, (uint16_t)res);
}

void modbus_store_process_pending(void)
{
    for (uint8_t i = 0; i < HYDRA_MODBUS_MAX_LIGHTS; ++i) {
        process_light_block(i);
    }
}

void modbus_store_refresh_status_mirrors(void)
{
    g_store[HYDRA_MODBUS_REG_REGISTERED_LIGHT_COUNT] = (uint16_t)light_registry_count();
    g_store[HYDRA_MODBUS_REG_REGISTERED_GROUP_COUNT] = (uint16_t)group_registry_count();

    for (uint8_t i = 0; i < HYDRA_MODBUS_MAX_LIGHTS; ++i) {
        const registered_light_t *light = light_registry_at(i);
        if (!light) {
            light_reg_set(i, HYDRA_LIGHT_OFF_PRESENT, 0);
            continue;
        }
        light_reg_set(i, HYDRA_LIGHT_OFF_PRESENT, 1);
        light_reg_set(i, HYDRA_LIGHT_OFF_ENABLED, light->enabled ? 1 : 0);
        int8_t rssi = light->last_seen_rssi;
        uint16_t abs_rssi = (rssi < 0) ? (uint16_t)(-rssi) : (uint16_t)rssi;
        light_reg_set(i, HYDRA_LIGHT_OFF_LAST_SEEN_RSSI_ABS, abs_rssi);
        light_reg_set(i, HYDRA_LIGHT_OFF_CMD_QUEUE_DEPTH,
                      (uint16_t)cmd_queue_depth(light->light_id));
        for (size_t c = 0; c < CHANNEL_COUNT; ++c) {
            light_reg_set(i, HYDRA_LIGHT_OFF_CURRENT_BRIGHTNESS + c, light->last_state.values[c]);
        }
        uint16_t power = light->last_state.values[0] > 0
            ? HYDRA_LIGHT_POWER_ON : HYDRA_LIGHT_POWER_OFF;
        light_reg_set(i, HYDRA_LIGHT_OFF_CURRENT_POWER, power);
    }
}

#ifdef ESP_PLATFORM
uint16_t *modbus_store_raw_array(void)
{
    return g_store;
}

static void modbus_store_apply_config_mirror(const config_modbus_t *mb, bool running)
{
    if (!mb) return;
    if (mb->enabled) {
        g_store[HYDRA_MODBUS_REG_CONFIG_FLAGS] |= HYDRA_CF_BIT_MODBUS_ENABLED;
    } else {
        g_store[HYDRA_MODBUS_REG_CONFIG_FLAGS] &= (uint16_t)~HYDRA_CF_BIT_MODBUS_ENABLED;
    }
    if (mb->master_mode_enabled) {
        g_store[HYDRA_MODBUS_REG_CONFIG_FLAGS] |= HYDRA_CF_BIT_MODBUS_MASTER_MODE;
    } else {
        g_store[HYDRA_MODBUS_REG_CONFIG_FLAGS] &= (uint16_t)~HYDRA_CF_BIT_MODBUS_MASTER_MODE;
    }

    if (!mb->enabled) {
        g_store[HYDRA_MODBUS_REG_MODBUS_STATUS] = HYDRA_MODBUS_STATUS_DISABLED;
    } else {
        g_store[HYDRA_MODBUS_REG_MODBUS_STATUS] =
            running ? HYDRA_MODBUS_STATUS_SLAVE_READY : HYDRA_MODBUS_STATUS_ERROR;
    }
}

esp_err_t modbus_interface_reconfigure(void)
{
    config_modbus_t mb;
    config_store_load_modbus(&mb);

    esp_err_t err = modbus_slave_driver_stop();
    if (err != ESP_OK) {
        g_store[HYDRA_MODBUS_REG_MODBUS_STATUS] = HYDRA_MODBUS_STATUS_ERROR;
        return err;
    }

    if (mb.enabled) {
        err = modbus_slave_driver_start();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "modbus_slave_driver_start failed: 0x%x", err);
            g_store[HYDRA_MODBUS_REG_MODBUS_STATUS] = HYDRA_MODBUS_STATUS_ERROR;
            modbus_store_apply_config_mirror(&mb, false);
            return err;
        }
    }

    modbus_store_apply_config_mirror(&mb, modbus_slave_driver_is_running());
    return ESP_OK;
}

esp_err_t modbus_interface_init(void)
{
    config_modbus_t mb;
    config_store_load_modbus(&mb);

    modbus_store_reset();
    modbus_store_apply_config_mirror(&mb, false);
    ESP_LOGI(TAG, "init: %d slots, magic=0x%04x",
             MODBUS_STORE_REG_COUNT, (unsigned)HYDRA_MODBUS_MAGIC_VALUE);

    esp_err_t err = ESP_OK;
    if (mb.enabled) {
        err = modbus_slave_driver_start();
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "modbus_slave_driver_start failed: 0x%x", err);
        g_store[HYDRA_MODBUS_REG_MODBUS_STATUS] = HYDRA_MODBUS_STATUS_ERROR;
    } else {
        modbus_store_apply_config_mirror(&mb, modbus_slave_driver_is_running());
    }
    return ESP_OK;
}
#endif
