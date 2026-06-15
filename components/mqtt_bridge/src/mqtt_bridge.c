#include "mqtt_bridge.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "cJSON.h"
#include "channel_model.h"
#include "preset_engine.h"

#ifdef ESP_PLATFORM
#include <stdio.h>

#include "config_store.h"
#include "esp_event.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "ha_discovery.h"
#include "light_registry.h"
#include "modbus_interface.h"
#include "modbus_registers.h"
static const char *TAG = "mqtt_bridge";

static esp_mqtt_client_handle_t s_client;
static config_mqtt_t s_cfg;
static config_controller_t s_controller_cfg;
static bool s_connected;
static char s_uri[128];
static char s_lwt_topic[128];
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

static void mqtt_status_set(bool enabled, bool connected)
{
    uint16_t status = HYDRA_MQTT_STATUS_DISABLED;
    if (enabled) status = connected ? HYDRA_MQTT_STATUS_CONNECTED : HYDRA_MQTT_STATUS_DISCONNECTED;

    modbus_store_set(HYDRA_MODBUS_REG_MQTT_STATUS, status);

    uint16_t cs = modbus_store_get(HYDRA_MODBUS_REG_CONTROLLER_STATUS);
    uint16_t cf = modbus_store_get(HYDRA_MODBUS_REG_CONFIG_FLAGS);
    if (enabled) {
        cs |= HYDRA_CS_BIT_MQTT_ENABLED;
        cf |= HYDRA_CF_BIT_MQTT_ENABLED;
    } else {
        cs &= (uint16_t)~HYDRA_CS_BIT_MQTT_ENABLED;
        cf &= (uint16_t)~HYDRA_CF_BIT_MQTT_ENABLED;
    }
    if (connected) {
        cs |= HYDRA_CS_BIT_MQTT_CONNECTED;
    } else {
        cs &= (uint16_t)~HYDRA_CS_BIT_MQTT_CONNECTED;
    }
    if (enabled && s_cfg.home_assistant_discovery) {
        cf |= HYDRA_CF_BIT_HA_DISCOVERY_ENABLED;
    } else {
        cf &= (uint16_t)~HYDRA_CF_BIT_HA_DISCOVERY_ENABLED;
    }
    modbus_store_set(HYDRA_MODBUS_REG_CONTROLLER_STATUS, cs);
    modbus_store_set(HYDRA_MODBUS_REG_CONFIG_FLAGS, cf);
}

static void base_prefix(char *out, size_t cap)
{
    snprintf(out, cap, "%s/%s", s_cfg.base_topic, s_controller_cfg.controller_id);
}

static int mqtt_publish_cb(const char *topic, const char *payload, void *user)
{
    (void)user;
    if (!s_client || !topic || !payload) return -1;
    return esp_mqtt_client_publish(s_client, topic, payload, 0, 1, 1);
}

static void mqtt_publish_availability(const char *value)
{
    if (!s_client || !s_connected) return;
    char topic[128];
    char base[96];
    base_prefix(base, sizeof base);
    snprintf(topic, sizeof topic, "%s/availability", base);
    esp_mqtt_client_publish(s_client, topic, value, 0, 1, 1);
}

static void mqtt_subscribe_commands(void)
{
    if (!s_client) return;
    char base[96];
    char topic[128];
    base_prefix(base, sizeof base);
    snprintf(topic, sizeof topic, "%s/light/+/set", base);
    esp_mqtt_client_subscribe(s_client, topic, 1);
    snprintf(topic, sizeof topic, "%s/group/+/set", base);
    esp_mqtt_client_subscribe(s_client, topic, 1);
}

static bool extract_target_from_topic(const char *topic,
                                      ce_target_type_t *target_type,
                                      char *target_id,
                                      size_t target_cap)
{
    if (!topic || !target_type || !target_id || target_cap == 0) return false;

    char light_prefix[128];
    char group_prefix[128];
    char base[96];
    base_prefix(base, sizeof base);
    snprintf(light_prefix, sizeof light_prefix, "%s/light/", base);
    snprintf(group_prefix, sizeof group_prefix, "%s/group/", base);

    const char *id_start = NULL;
    if (strncmp(topic, light_prefix, strlen(light_prefix)) == 0) {
        *target_type = CE_TARGET_LIGHT;
        id_start = topic + strlen(light_prefix);
    } else if (strncmp(topic, group_prefix, strlen(group_prefix)) == 0) {
        *target_type = CE_TARGET_GROUP;
        id_start = topic + strlen(group_prefix);
    } else {
        return false;
    }

    const char *suffix = strstr(id_start, "/set");
    if (!suffix || suffix[4] != '\0') return false;
    size_t n = (size_t)(suffix - id_start);
    if (n == 0 || n >= target_cap) return false;
    memcpy(target_id, id_start, n);
    target_id[n] = '\0';
    return true;
}

static void publish_command_result(ce_target_type_t target_type,
                                   const char *target_id,
                                   ce_result_t result,
                                   const char *command_id)
{
    if (!s_client || !s_connected || !target_id) return;
    char base[96];
    char topic[160];
    char payload[160];
    base_prefix(base, sizeof base);
    snprintf(topic, sizeof topic, "%s/%s/%s/result", base,
             target_type == CE_TARGET_GROUP ? "group" : "light", target_id);
    snprintf(payload, sizeof payload,
             "{\"result\":%d,\"command_id\":\"%s\"}",
             (int)result, command_id ? command_id : "");
    esp_mqtt_client_publish(s_client, topic, payload, 0, 1, 0);
}

static void handle_mqtt_data(const esp_mqtt_event_handle_t event)
{
    if (!event || event->topic_len <= 0 || event->data_len <= 0) return;
    char topic[192];
    char payload[768];
    size_t topic_len = (size_t)event->topic_len;
    size_t data_len = (size_t)event->data_len;
    if (topic_len >= sizeof topic) topic_len = sizeof topic - 1;
    if (data_len >= sizeof payload) data_len = sizeof payload - 1;
    memcpy(topic, event->topic, topic_len);
    topic[topic_len] = '\0';
    memcpy(payload, event->data, data_len);
    payload[data_len] = '\0';

    ce_target_type_t target_type = CE_TARGET_LIGHT;
    char target_id[LIGHT_ID_LEN];
    if (!extract_target_from_topic(topic, &target_type, target_id, sizeof target_id)) {
        ESP_LOGW(TAG, "ignored topic: %s", topic);
        return;
    }

    ce_request_t req;
    if (mqtt_parse_light_command(payload, target_id, &req) != 0) {
        publish_command_result(target_type, target_id, CE_RESULT_INVALID_COMMAND, "");
        return;
    }
    req.source = CMD_SOURCE_MQTT;
    req.target_type = target_type;
    strncpy(req.target_id, target_id, LIGHT_ID_LEN - 1);
    req.target_id[LIGHT_ID_LEN - 1] = '\0';

    char cmd_id[CMD_ID_LEN];
    ce_result_t result = command_engine_submit(&req, cmd_id);
    publish_command_result(target_type, target_id, result, cmd_id);
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    (void)handler_args;
    (void)base;
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            s_connected = true;
            mqtt_status_set(s_cfg.enabled, true);
            mqtt_publish_availability("online");
            mqtt_subscribe_commands();
            if (s_cfg.home_assistant_discovery) {
                char ha_base[96];
                base_prefix(ha_base, sizeof ha_base);
                ha_discovery_publish_all(s_controller_cfg.controller_id,
                                         ha_base,
                                         s_cfg.home_assistant_prefix,
                                         mqtt_publish_cb, NULL);
            }
            ESP_LOGI(TAG, "connected");
            break;
        case MQTT_EVENT_DISCONNECTED:
            s_connected = false;
            mqtt_status_set(s_cfg.enabled, false);
            ESP_LOGW(TAG, "disconnected");
            break;
        case MQTT_EVENT_DATA:
            handle_mqtt_data(event);
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGW(TAG, "mqtt event error");
            break;
        default:
            break;
    }
}

static esp_err_t mqtt_bridge_stop_client(void)
{
    if (!s_client) return ESP_OK;
    mqtt_publish_availability("offline");
    esp_err_t stop_err = esp_mqtt_client_stop(s_client);
    esp_err_t destroy_err = esp_mqtt_client_destroy(s_client);
    s_client = NULL;
    s_connected = false;
    if (stop_err != ESP_OK) return stop_err;
    return destroy_err;
}

esp_err_t mqtt_bridge_reconfigure(void)
{
    config_store_load_mqtt(&s_cfg);
    config_store_load_controller(&s_controller_cfg);

    mqtt_bridge_stop_client();
    mqtt_status_set(s_cfg.enabled, false);

    if (!s_cfg.enabled) {
        ESP_LOGI(TAG, "disabled by config");
        return ESP_OK;
    }
    if (s_cfg.host[0] == '\0') {
        ESP_LOGW(TAG, "enabled but broker host is empty");
        return ESP_OK;
    }

    snprintf(s_uri, sizeof s_uri, "%s://%s:%u",
             s_cfg.use_tls ? "mqtts" : "mqtt",
             s_cfg.host, (unsigned)s_cfg.port);
    char base[96];
    base_prefix(base, sizeof base);
    snprintf(s_lwt_topic, sizeof s_lwt_topic, "%s/availability", base);

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = s_uri,
        .credentials.username = s_cfg.username[0] ? s_cfg.username : NULL,
        .credentials.client_id = s_cfg.client_id[0] ? s_cfg.client_id : NULL,
        .credentials.authentication.password = s_cfg.password[0] ? s_cfg.password : NULL,
        .session.keepalive = s_cfg.keepalive_sec ? s_cfg.keepalive_sec : 60,
        .session.last_will.topic = s_lwt_topic,
        .session.last_will.msg = "offline",
        .session.last_will.msg_len = 7,
        .session.last_will.qos = 1,
        .session.last_will.retain = true,
    };

    s_client = esp_mqtt_client_init(&mqtt_cfg);
    if (!s_client) {
        mqtt_status_set(true, false);
        return ESP_ERR_NO_MEM;
    }
    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_err_t err = esp_mqtt_client_start(s_client);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_mqtt_client_start failed: 0x%x", err);
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
        mqtt_status_set(true, false);
        return err;
    }
    mqtt_status_set(true, false);
    ESP_LOGI(TAG, "started client uri=%s", s_uri);
    return ESP_OK;
}

bool mqtt_bridge_is_connected(void)
{
    return s_connected;
}

esp_err_t mqtt_bridge_init(void)
{
    return mqtt_bridge_reconfigure();
}
#endif
