/* config_store: NVS-backed persistence for controller / modbus / wifi /
 * mqtt / user color profiles. One namespace per category, one blob key "rec"
 * per namespace. Each blob is prefixed with a uint32_t schema_version
 * so layout changes invalidate stored data and we fall back to
 * compile-time defaults instead of dereferencing a stale struct.
 *
 * Init is a no-op beyond confirming NVS is available; namespaces are
 * opened lazily by load/save calls. Profiles use a fixed array (count + entries)
 * to store up to MAX_USER_PROFILES named channel_state_t (full 9-channel intensity
 * mixes for LiveDemoScene). */

#include "config_store.h"

#include <stdlib.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "config_store";

#define NVS_KEY  "rec"

#define NS_CONTROLLER  "ctrl_cfg"
#define NS_MODBUS      "mb_cfg"
#define NS_WIFI        "wifi_cfg"
#define NS_MQTT        "mqtt_cfg"
#define NS_PROFILES    "prof_cfg"

static bool s_reported_controller_fallback;
static bool s_reported_modbus_fallback;
static bool s_reported_wifi_fallback;
static bool s_reported_mqtt_fallback;
static bool s_reported_profiles_fallback;

/* ---- blob helpers ---- */

static esp_err_t load_blob(const char *ns,
                           uint32_t expected_version,
                           void *out,
                           size_t expected_size)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(ns, NVS_READONLY, &h);
    if (err != ESP_OK) return err;

    size_t blob_size = sizeof(uint32_t) + expected_size;
    uint8_t *buf = (uint8_t *)malloc(blob_size);
    if (!buf) { nvs_close(h); return ESP_ERR_NO_MEM; }

    size_t got = blob_size;
    err = nvs_get_blob(h, NVS_KEY, buf, &got);
    nvs_close(h);
    if (err != ESP_OK) { free(buf); return err; }
    if (got != blob_size) { free(buf); return ESP_ERR_INVALID_SIZE; }

    uint32_t ver;
    memcpy(&ver, buf, sizeof ver);
    if (ver != expected_version) { free(buf); return ESP_ERR_INVALID_VERSION; }

    memcpy(out, buf + sizeof(uint32_t), expected_size);
    free(buf);
    return ESP_OK;
}

static esp_err_t save_blob(const char *ns,
                           uint32_t version,
                           const void *in,
                           size_t size)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(ns, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;

    size_t blob_size = sizeof(uint32_t) + size;
    uint8_t *buf = (uint8_t *)malloc(blob_size);
    if (!buf) { nvs_close(h); return ESP_ERR_NO_MEM; }

    memcpy(buf, &version, sizeof version);
    memcpy(buf + sizeof(uint32_t), in, size);

    err = nvs_set_blob(h, NVS_KEY, buf, blob_size);
    free(buf);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

static bool expected_default_err(esp_err_t err)
{
    return err == ESP_ERR_NVS_NOT_FOUND ||
           err == ESP_ERR_INVALID_VERSION ||
           err == ESP_ERR_INVALID_SIZE;
}

static void report_load_fallback(const char *name, esp_err_t load_err,
                                 esp_err_t save_err, bool *reported)
{
    if (reported && *reported) return;
    if (reported) *reported = true;

    if (expected_default_err(load_err)) {
        if (save_err == ESP_OK) {
            ESP_LOGI(TAG, "%s config missing or outdated (0x%x); saved defaults",
                     name, load_err);
        } else {
            ESP_LOGW(TAG, "%s config missing or outdated (0x%x); using defaults, save failed 0x%x",
                     name, load_err, save_err);
        }
    } else {
        ESP_LOGW(TAG, "%s load err=0x%x; using defaults", name, load_err);
    }
}

static esp_err_t erase_namespace(const char *ns)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(ns, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_erase_all(h);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

/* ---- public API ---- */

esp_err_t config_store_init(void)
{
    ESP_LOGI(TAG, "init schema versions: ctrl=%u mb=%u wifi=%u mqtt=%u prof=%u",
             (unsigned)CONFIG_SCHEMA_CONTROLLER,
             (unsigned)CONFIG_SCHEMA_MODBUS,
             (unsigned)CONFIG_SCHEMA_WIFI,
             (unsigned)CONFIG_SCHEMA_MQTT,
             (unsigned)CONFIG_SCHEMA_PROFILES);
    return ESP_OK;
}

esp_err_t config_store_load_controller(config_controller_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    esp_err_t err = load_blob(NS_CONTROLLER, CONFIG_SCHEMA_CONTROLLER, out, sizeof *out);
    if (err != ESP_OK) {
        config_defaults_controller(out);
        esp_err_t save_err = expected_default_err(err)
            ? save_blob(NS_CONTROLLER, CONFIG_SCHEMA_CONTROLLER, out, sizeof *out)
            : ESP_OK;
        report_load_fallback("controller", err, save_err, &s_reported_controller_fallback);
        return ESP_OK;
    }
    return ESP_OK;
}

esp_err_t config_store_save_controller(const config_controller_t *in)
{
    if (!in) return ESP_ERR_INVALID_ARG;
    return save_blob(NS_CONTROLLER, CONFIG_SCHEMA_CONTROLLER, in, sizeof *in);
}

esp_err_t config_store_load_modbus(config_modbus_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    esp_err_t err = load_blob(NS_MODBUS, CONFIG_SCHEMA_MODBUS, out, sizeof *out);
    if (err != ESP_OK) {
        config_defaults_modbus(out);
        esp_err_t save_err = expected_default_err(err)
            ? save_blob(NS_MODBUS, CONFIG_SCHEMA_MODBUS, out, sizeof *out)
            : ESP_OK;
        report_load_fallback("modbus", err, save_err, &s_reported_modbus_fallback);
        return ESP_OK;
    }
    return ESP_OK;
}

esp_err_t config_store_save_modbus(const config_modbus_t *in)
{
    if (!in) return ESP_ERR_INVALID_ARG;
    return save_blob(NS_MODBUS, CONFIG_SCHEMA_MODBUS, in, sizeof *in);
}

esp_err_t config_store_load_wifi(config_wifi_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    esp_err_t err = load_blob(NS_WIFI, CONFIG_SCHEMA_WIFI, out, sizeof *out);
    if (err != ESP_OK) {
        config_defaults_wifi(out);
        esp_err_t save_err = expected_default_err(err)
            ? save_blob(NS_WIFI, CONFIG_SCHEMA_WIFI, out, sizeof *out)
            : ESP_OK;
        report_load_fallback("wifi", err, save_err, &s_reported_wifi_fallback);
        return ESP_OK;
    }
    return ESP_OK;
}

esp_err_t config_store_save_wifi(const config_wifi_t *in)
{
    if (!in) return ESP_ERR_INVALID_ARG;
    return save_blob(NS_WIFI, CONFIG_SCHEMA_WIFI, in, sizeof *in);
}

esp_err_t config_store_load_mqtt(config_mqtt_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    esp_err_t err = load_blob(NS_MQTT, CONFIG_SCHEMA_MQTT, out, sizeof *out);
    if (err != ESP_OK) {
        config_defaults_mqtt(out);
        esp_err_t save_err = expected_default_err(err)
            ? save_blob(NS_MQTT, CONFIG_SCHEMA_MQTT, out, sizeof *out)
            : ESP_OK;
        report_load_fallback("mqtt", err, save_err, &s_reported_mqtt_fallback);
        return ESP_OK;
    }
    return ESP_OK;
}

esp_err_t config_store_save_mqtt(const config_mqtt_t *in)
{
    if (!in) return ESP_ERR_INVALID_ARG;
    return save_blob(NS_MQTT, CONFIG_SCHEMA_MQTT, in, sizeof *in);
}

esp_err_t config_store_load_profiles(config_profiles_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    esp_err_t err = load_blob(NS_PROFILES, CONFIG_SCHEMA_PROFILES, out, sizeof *out);
    if (err != ESP_OK) {
        config_defaults_profiles(out);
        esp_err_t save_err = expected_default_err(err)
            ? save_blob(NS_PROFILES, CONFIG_SCHEMA_PROFILES, out, sizeof *out)
            : ESP_OK;
        report_load_fallback("profiles", err, save_err, &s_reported_profiles_fallback);
        return ESP_OK;
    }
    return ESP_OK;
}

esp_err_t config_store_save_profiles(const config_profiles_t *in)
{
    if (!in) return ESP_ERR_INVALID_ARG;
    return save_blob(NS_PROFILES, CONFIG_SCHEMA_PROFILES, in, sizeof *in);
}

esp_err_t config_store_factory_reset(void)
{
    esp_err_t e1 = erase_namespace(NS_CONTROLLER);
    esp_err_t e2 = erase_namespace(NS_MODBUS);
    esp_err_t e3 = erase_namespace(NS_WIFI);
    esp_err_t e4 = erase_namespace(NS_MQTT);
    esp_err_t e5 = erase_namespace(NS_PROFILES);
    if (e1 != ESP_OK) return e1;
    if (e2 != ESP_OK) return e2;
    if (e3 != ESP_OK) return e3;
    if (e4 != ESP_OK) return e4;
    return e5;
}
