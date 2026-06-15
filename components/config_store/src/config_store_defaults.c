/* Pure-C defaults for the four built-in config records. Values come from
 * docs/esp32-hydra64hd-controller-plan.md "Persistent Data Model"
 * section. Keep field defaults in sync with that spec; the host tests
 * pin specific values to specific spec lines. */

#include "config_store.h"

#include <string.h>

void config_defaults_controller(config_controller_t *out)
{
    if (!out) return;
    memset(out, 0, sizeof *out);
    strncpy(out->controller_id,    "hydra-ctrl-01",     sizeof out->controller_id - 1);
    strncpy(out->device_name,      "Hydra Controller",  sizeof out->device_name - 1);
    strncpy(out->timezone,         "local",             sizeof out->timezone - 1);
    strncpy(out->firmware_version, "0.1.0",             sizeof out->firmware_version - 1);
    out->ota_enabled             = true;
    out->max_registered_lights   = 4;
    out->ble_idle_disconnect_ms  = 30000;
    out->ble_command_concurrency = 1;
}

void config_defaults_modbus(config_modbus_t *out)
{
    if (!out) return;
    memset(out, 0, sizeof *out);
    out->enabled              = false;
    out->master_mode_enabled  = false;
    out->slave_address        = 10;
    out->baud_rate            = 19200;
    out->data_bits            = 8;
    out->parity               = MODBUS_PARITY_EVEN;
    out->stop_bits            = 1;
    out->uart_port            = 1;
    /* Default RS485 wiring for this controller build. */
    out->tx_pin               = 17;
    out->rx_pin               = 18;
    out->rts_de_pin           = 4;
    out->response_timeout_ms  = 250;
    out->command_watchdog_ms  = 30000;
}

void config_defaults_wifi(config_wifi_t *out)
{
    if (!out) return;
    memset(out, 0, sizeof *out);
    out->enabled             = true;
    out->ssid[0]             = '\0';
    out->password[0]         = '\0';
    out->ap_fallback_enabled = true;
    strncpy(out->ap_ssid,     "HydraBridge-Setup", sizeof out->ap_ssid - 1);
    strncpy(out->ap_password, "hydrabridge",       sizeof out->ap_password - 1);
}

void config_defaults_mqtt(config_mqtt_t *out)
{
    if (!out) return;
    memset(out, 0, sizeof *out);
    out->enabled                  = false;
    out->host[0]                  = '\0';
    out->port                     = 1883;
    out->use_tls                  = false;
    out->username[0]              = '\0';
    out->password[0]              = '\0';
    strncpy(out->client_id,             "hydrabridge-esp32", sizeof out->client_id - 1);
    out->keepalive_sec            = 60;
    strncpy(out->base_topic,            "aihydra",       sizeof out->base_topic - 1);
    out->home_assistant_discovery = false;
    strncpy(out->home_assistant_prefix, "homeassistant", sizeof out->home_assistant_prefix - 1);
}

void config_defaults_profiles(config_profiles_t *out)
{
    if (!out) return;
    memset(out, 0, sizeof *out);
    /* No built-in user profiles by default; user saves via UI or API.
     * count=0 means empty list; falls back cleanly. */
}
