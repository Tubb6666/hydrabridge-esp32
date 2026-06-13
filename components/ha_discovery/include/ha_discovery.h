#ifndef HYDRA_HA_DISCOVERY_H
#define HYDRA_HA_DISCOVERY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "channel_model.h"
#include "light_registry.h"
#include "preset_engine.h"

/* Home Assistant MQTT discovery topic + payload builders. Per
 * registered light: 1 power switch, 9 channel numbers, 1 preset
 * select.
 *
 * Topic shape: <ha_prefix>/<component>/<unique_id>/config
 * The JSON builders are pure C using snprintf -- host-testable. */

#define HA_DISC_TOPIC_MAX        160
#define HA_DISC_PAYLOAD_MAX      512
#define HA_DISC_UNIQUE_ID_MAX     80

size_t ha_disc_unique_id(const char *controller_id,
                         const registered_light_t *light,
                         const char *entity_suffix,
                         char *out, size_t cap);

size_t ha_disc_topic(const char *ha_prefix, const char *component,
                     const char *unique_id, char *out, size_t cap);

size_t ha_disc_power_switch_json(const registered_light_t *light,
                                 const char *controller_id,
                                 const char *base_topic,
                                 char *out, size_t cap);

size_t ha_disc_channel_number_json(const registered_light_t *light,
                                   const char *controller_id,
                                   const char *base_topic,
                                   const char *channel_name,
                                   char *out, size_t cap);

size_t ha_disc_preset_select_json(const registered_light_t *light,
                                  const char *controller_id,
                                  const char *base_topic,
                                  char *out, size_t cap);

#ifdef ESP_PLATFORM
#include "esp_err.h"
typedef int (*ha_disc_publish_fn)(const char *topic, const char *payload, void *user);
esp_err_t ha_discovery_publish_all(const char *controller_id,
                                   const char *base_topic,
                                   const char *ha_prefix,
                                   ha_disc_publish_fn publish_fn,
                                   void *user);
#endif

#endif /* HYDRA_HA_DISCOVERY_H */
