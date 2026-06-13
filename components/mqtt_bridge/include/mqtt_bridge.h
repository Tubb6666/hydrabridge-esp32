#ifndef HYDRA_MQTT_BRIDGE_H
#define HYDRA_MQTT_BRIDGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "command_engine.h"

/* mqtt_bridge: JSON parser for MQTT light commands. The MQTT client
 * lifecycle (connect/subscribe/publish) lives in the ESP-IDF build;
 * the parser is pure C so it's host-testable against every payload
 * shape in the spec L1138-L1208. */

int mqtt_parse_light_command(const char *json_text,
                             const char *target_light_id,
                             ce_request_t *out);

#ifdef ESP_PLATFORM
#include "esp_err.h"
esp_err_t mqtt_bridge_init(void);
#endif

#endif /* HYDRA_MQTT_BRIDGE_H */
