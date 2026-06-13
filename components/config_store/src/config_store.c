/* config_store: NVS-backed persistence for controller / modbus / wifi /
 * mqtt config records. One namespace per category, one blob key "rec"
 * per namespace. Each blob is prefixed with a uint32_t schema_version
 * so layout changes invalidate stored data and we fall back to
 * compile-time defaults instead of dereferencing a stale struct.
 *
 * Init is a no-op beyond confirming NVS is available; namespaces are
 * opened lazily by load/save calls. */

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
    ESP_LOGI(TAG, "init schema versions: ctrl=%u mb=%u wifi=%u mqtt=%u",
             (unsigned)CONFIG_SCHEMA_CONTROLLER,
             (unsigned)CONFIG_SCHEMA_MODBUS,
             (unsigned)CONFIG_SCHEMA_WIFI,
             (unsigned)CONFIG_SCHEMA_MQTT);
    return ESP_OK;
}

esp_err_t config_store_load_controller(config_controller_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    esp_err_t err = load_blob(NS_CONTROLLER, CONFIG_SCHEMA_CONTROLLER, out, sizeof *out);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "controller load err=0x%x; using defaults", err);
        config_defaults_controller(out);
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
        ESP_LOGW(TAG, "modbus load err=0x%x; using defaults", err);
        config_defaults_modbus(out);
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
        ESP_LOGW(TAG, "wifi load err=0x%x; using defaults", err);
        config_defaults_wifi(out);
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
        ESP_LOGW(TAG, "mqtt load err=0x%x; using defaults", err);
        config_defaults_mqtt(out);
        return ESP_OK;
    }
    return ESP_OK;
}

esp_err_t config_store_save_mqtt(const config_mqtt_t *in)
{
    if (!in) return ESP_ERR_INVALID_ARG;
    return save_blob(NS_MQTT, CONFIG_SCHEMA_MQTT, in, sizeof *in);
}

esp_err_t config_store_factory_reset(void)
{
    esp_err_t e1 = erase_namespace(NS_CONTROLLER);
    esp_err_t e2 = erase_namespace(NS_MODBUS);
    esp_err_t e3 = erase_namespace(NS_WIFI);
    esp_err_t e4 = erase_namespace(NS_MQTT);
    if (e1 != ESP_OK) return e1;
    if (e2 != ESP_OK) return e2;
    if (e3 != ESP_OK) return e3;
    return e4;
}
