#include "mqtt_bridge.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "cJSON.h"
#include "channel_model.h"
#include "preset_engine.h"

#ifdef ESP_PLATFORM
#include "esp_log.h"
static const char *TAG = "mqtt_bridge";
#endif

static void copy_str(char *dst, size_t cap, const char *src)
{
    if (!dst || cap == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    size_t n = strlen(src);
    if (n >= cap) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static void apply_common_metadata(const cJSON *root, ce_request_t *r)
{
    const cJSON *timeout = cJSON_GetObjectItemCaseSensitive(root, "timeout");
    if (cJSON_IsNumber(timeout) && timeout->valuedouble > 0) {
        r->scene_timeout_sec = (uint16_t)timeout->valuedouble;
    }
    const cJSON *cid = cJSON_GetObjectItemCaseSensitive(root, "command_id");
    if (cJSON_IsString(cid) && cid->valuestring) {
        copy_str(r->command_id, CMD_ID_LEN, cid->valuestring);
    }
}

static int parse_power(const cJSON *power_node, ce_request_t *r)
{
    if (!cJSON_IsString(power_node) || !power_node->valuestring) return -1;
    if (strcmp(power_node->valuestring, "on")  == 0) { r->kind = CE_KIND_POWER_ON;  return 0; }
    if (strcmp(power_node->valuestring, "off") == 0) { r->kind = CE_KIND_POWER_OFF; return 0; }
    return -1;
}

static int parse_preset(const cJSON *preset_node, ce_request_t *r)
{
    if (!cJSON_IsString(preset_node) || !preset_node->valuestring) return -1;
    preset_id_t id = preset_by_name(preset_node->valuestring);
    if (id == PRESET_NONE) return -1;
    r->kind   = CE_KIND_PRESET;
    r->preset = id;
    return 0;
}

static int parse_channels(const cJSON *root, ce_request_t *r)
{
    const cJSON *channels = cJSON_GetObjectItemCaseSensitive(root, "channels");
    if (!cJSON_IsObject(channels)) return -1;
    const cJSON *replace = cJSON_GetObjectItemCaseSensitive(root, "replace");
    r->replace = cJSON_IsBool(replace) ? cJSON_IsTrue(replace) : true;

    r->channel_count = 0;
    const cJSON *child = NULL;
    cJSON_ArrayForEach(child, channels) {
        if (r->channel_count >= CHANNEL_COUNT) return -1;
        if (!cJSON_IsNumber(child) || !child->string) return -1;
        /* Store the canonical channel_def->name pointer (static const
         * storage) rather than child->string -- cJSON_Delete frees the
         * latter while ce_request_t may be used after we return. */
        const channel_def_t *def = channel_model_by_name(child->string);
        if (!def) return -1;
        r->channels[r->channel_count].name  = def->name;
        r->channels[r->channel_count].value = (uint16_t)child->valuedouble;
        ++r->channel_count;
    }
    if (r->channel_count == 0) return -1;
    r->kind = CE_KIND_SET_CHANNELS;
    return 0;
}

static int parse_ramp(const cJSON *ramp_node, ce_request_t *r)
{
    if (!cJSON_IsObject(ramp_node)) return -1;
    const cJSON *from = cJSON_GetObjectItemCaseSensitive(ramp_node, "from");
    const cJSON *to   = cJSON_GetObjectItemCaseSensitive(ramp_node, "to");
    const cJSON *dur  = cJSON_GetObjectItemCaseSensitive(ramp_node, "duration_ms");
    const cJSON *stp  = cJSON_GetObjectItemCaseSensitive(ramp_node, "steps");
    if (!cJSON_IsNumber(from) || !cJSON_IsNumber(to) ||
        !cJSON_IsNumber(dur)  || !cJSON_IsNumber(stp)) return -1;
    r->kind             = CE_KIND_RAMP;
    r->ramp_from        = (uint16_t)from->valuedouble;
    r->ramp_to          = (uint16_t)to->valuedouble;
    r->ramp_duration_ms = (uint32_t)dur->valuedouble;
    r->ramp_steps       = (uint16_t)stp->valuedouble;
    return 0;
}

int mqtt_parse_light_command(const char *json_text,
                             const char *target_light_id,
                             ce_request_t *out)
{
    if (!json_text || !out || !target_light_id) return -1;

    memset(out, 0, sizeof *out);
    out->source = CMD_SOURCE_MQTT;
    copy_str(out->target_id, LIGHT_ID_LEN, target_light_id);

    cJSON *root = cJSON_Parse(json_text);
    if (!root) return -1;

    int rc = -1;
    do {
        if (!cJSON_IsObject(root)) break;

        apply_common_metadata(root, out);

        const cJSON *power    = cJSON_GetObjectItemCaseSensitive(root, "power");
        const cJSON *preset   = cJSON_GetObjectItemCaseSensitive(root, "preset");
        const cJSON *ramp     = cJSON_GetObjectItemCaseSensitive(root, "ramp");
        const cJSON *channels = cJSON_GetObjectItemCaseSensitive(root, "channels");

        if (ramp)     { if (parse_ramp(ramp, out)        != 0) break; rc = 0; break; }
        if (preset)   { if (parse_preset(preset, out)    != 0) break; rc = 0; break; }
        if (power)    { if (parse_power(power, out)      != 0) break; rc = 0; break; }
        if (channels) { if (parse_channels(root, out)    != 0) break; rc = 0; break; }
    } while (0);

    cJSON_Delete(root);
    return rc;
}

#ifdef ESP_PLATFORM
esp_err_t mqtt_bridge_init(void)
{
    ESP_LOGI(TAG, "init");
    return ESP_OK;
}
#endif
