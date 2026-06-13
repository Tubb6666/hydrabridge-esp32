#include "ha_discovery.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_log.h"
static const char *TAG = "ha_discovery";
#endif

size_t ha_disc_unique_id(const char *controller_id,
                         const registered_light_t *light,
                         const char *entity_suffix,
                         char *out, size_t cap)
{
    if (!out || !controller_id || !light || !entity_suffix) return 0;
    int n = snprintf(out, cap, "%s_%s_%s",
                     controller_id, light->light_id, entity_suffix);
    if (n < 0 || (size_t)n >= cap) return 0;
    for (char *p = out; *p; ++p) if (*p == '-') *p = '_';
    return (size_t)n;
}

size_t ha_disc_topic(const char *ha_prefix, const char *component,
                     const char *unique_id, char *out, size_t cap)
{
    if (!out || !ha_prefix || !component || !unique_id) return 0;
    int n = snprintf(out, cap, "%s/%s/%s/config",
                     ha_prefix, component, unique_id);
    if (n < 0 || (size_t)n >= cap) return 0;
    return (size_t)n;
}

size_t ha_disc_power_switch_json(const registered_light_t *light,
                                 const char *controller_id,
                                 const char *base_topic,
                                 char *out, size_t cap)
{
    if (!out || !light || !controller_id || !base_topic) return 0;
    char uid[HA_DISC_UNIQUE_ID_MAX];
    if (!ha_disc_unique_id(controller_id, light, "power", uid, sizeof uid)) return 0;
    int n = snprintf(out, cap,
        "{\"name\":\"%s Power\","
        "\"uniq_id\":\"%s\","
        "\"cmd_t\":\"%s/light/%s/set\","
        "\"stat_t\":\"%s/light/%s/state\","
        "\"avty_t\":\"%s/light/%s/availability\","
        "\"pl_on\":\"{\\\"power\\\":\\\"on\\\"}\","
        "\"pl_off\":\"{\\\"power\\\":\\\"off\\\"}\","
        "\"val_tpl\":\"{{ value_json.power }}\","
        "\"stat_on\":\"on\",\"stat_off\":\"off\","
        "\"dev\":{\"ids\":[\"%s\"],\"name\":\"%s\",\"mf\":\"Aqua Illumination\",\"mdl\":\"Hydra 64HD\"}}",
        light->display_name, uid,
        base_topic, light->light_id,
        base_topic, light->light_id,
        base_topic, light->light_id,
        light->light_id, light->display_name);
    if (n < 0 || (size_t)n >= cap) return 0;
    return (size_t)n;
}

size_t ha_disc_channel_number_json(const registered_light_t *light,
                                   const char *controller_id,
                                   const char *base_topic,
                                   const char *channel_name,
                                   char *out, size_t cap)
{
    if (!out || !light || !controller_id || !base_topic || !channel_name) return 0;
    const channel_def_t *def = channel_model_by_name(channel_name);
    if (!def) return 0;
    char uid[HA_DISC_UNIQUE_ID_MAX];
    if (!ha_disc_unique_id(controller_id, light, channel_name, uid, sizeof uid)) return 0;
    int n = snprintf(out, cap,
        "{\"name\":\"%s %s\","
        "\"uniq_id\":\"%s\","
        "\"cmd_t\":\"%s/light/%s/set\","
        "\"cmd_tpl\":\"{\\\"channels\\\":{\\\"%s\\\":{{ value | int }}}}\","
        "\"stat_t\":\"%s/light/%s/state\","
        "\"val_tpl\":\"{{ value_json.channels.%s }}\","
        "\"min\":%u,\"max\":%u,\"step\":1,"
        "\"dev\":{\"ids\":[\"%s\"]}}",
        light->display_name, def->label, uid,
        base_topic, light->light_id, channel_name,
        base_topic, light->light_id, channel_name,
        (unsigned)def->min, (unsigned)def->max,
        light->light_id);
    if (n < 0 || (size_t)n >= cap) return 0;
    return (size_t)n;
}

size_t ha_disc_preset_select_json(const registered_light_t *light,
                                  const char *controller_id,
                                  const char *base_topic,
                                  char *out, size_t cap)
{
    if (!out || !light || !controller_id || !base_topic) return 0;
    char uid[HA_DISC_UNIQUE_ID_MAX];
    if (!ha_disc_unique_id(controller_id, light, "preset", uid, sizeof uid)) return 0;

    size_t count = 0;
    const preset_info_t *all = preset_all(&count);

    char options[256];
    size_t off = 0;
    for (size_t i = 0; i < count; ++i) {
        int n = snprintf(options + off, sizeof options - off,
                         "%s\"%s\"", i == 0 ? "" : ",", all[i].name);
        if (n < 0 || (size_t)n >= sizeof options - off) return 0;
        off += (size_t)n;
    }

    int n = snprintf(out, cap,
        "{\"name\":\"%s Preset\","
        "\"uniq_id\":\"%s\","
        "\"cmd_t\":\"%s/light/%s/set\","
        "\"cmd_tpl\":\"{\\\"preset\\\":\\\"{{ value }}\\\"}\","
        "\"options\":[%s],"
        "\"dev\":{\"ids\":[\"%s\"]}}",
        light->display_name, uid,
        base_topic, light->light_id,
        options, light->light_id);
    if (n < 0 || (size_t)n >= cap) return 0;
    return (size_t)n;
}

#ifdef ESP_PLATFORM

static int publish_one(ha_disc_publish_fn fn, void *user,
                       const char *ha_prefix, const char *component,
                       const char *unique_id, const char *payload)
{
    char topic[HA_DISC_TOPIC_MAX];
    if (!ha_disc_topic(ha_prefix, component, unique_id, topic, sizeof topic)) return -1;
    return fn(topic, payload, user);
}

esp_err_t ha_discovery_publish_all(const char *controller_id,
                                   const char *base_topic,
                                   const char *ha_prefix,
                                   ha_disc_publish_fn publish_fn,
                                   void *user)
{
    if (!controller_id || !base_topic || !ha_prefix || !publish_fn) return ESP_ERR_INVALID_ARG;

    char payload[HA_DISC_PAYLOAD_MAX];
    char uid[HA_DISC_UNIQUE_ID_MAX];

    size_t n = light_registry_count();
    for (size_t i = 0; i < n; ++i) {
        const registered_light_t *light = light_registry_at(i);
        if (!light) continue;

        if (ha_disc_power_switch_json(light, controller_id, base_topic, payload, sizeof payload) &&
            ha_disc_unique_id(controller_id, light, "power", uid, sizeof uid)) {
            publish_one(publish_fn, user, ha_prefix, "switch", uid, payload);
        }

        const channel_def_t *defs = channel_model_all();
        for (int c = 0; c < CHANNEL_COUNT; ++c) {
            if (ha_disc_channel_number_json(light, controller_id, base_topic,
                                            defs[c].name, payload, sizeof payload) &&
                ha_disc_unique_id(controller_id, light, defs[c].name, uid, sizeof uid)) {
                publish_one(publish_fn, user, ha_prefix, "number", uid, payload);
            }
        }

        if (ha_disc_preset_select_json(light, controller_id, base_topic, payload, sizeof payload) &&
            ha_disc_unique_id(controller_id, light, "preset", uid, sizeof uid)) {
            publish_one(publish_fn, user, ha_prefix, "select", uid, payload);
        }
    }
    ESP_LOGI(TAG, "published discovery for %u lights", (unsigned)n);
    return ESP_OK;
}

#endif /* ESP_PLATFORM */
