#ifdef ESP_PLATFORM

#include "wifi_station.h"

#include <stdbool.h>
#include <string.h>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mdns.h"

/* Compile-time SSID + PSK from a user-supplied header. The header is
 * gitignored; main/wifi_credentials.h.example is the committed
 * template. */
#if __has_include("wifi_credentials.h")
#include "wifi_credentials.h"
#else
#error "Create main/wifi_credentials.h from main/wifi_credentials.h.example"
#endif

#ifndef HYDRA_WIFI_SSID
#error "HYDRA_WIFI_SSID is not defined in wifi_credentials.h"
#endif
#ifndef HYDRA_WIFI_PSK
#error "HYDRA_WIFI_PSK is not defined in wifi_credentials.h"
#endif
#ifndef HYDRA_MDNS_HOSTNAME
#define HYDRA_MDNS_HOSTNAME "hydra-ctrl"
#endif

static const char *TAG = "wifi_station";

#define RECONNECT_DELAY_MS 5000

static bool         s_started   = false;
static bool         s_connected = false;
static bool         s_paused    = false;
static esp_netif_t *s_netif     = NULL;

bool hydra_wifi_is_connected(void) { return s_connected; }

static void on_wifi_event(void *arg, esp_event_base_t base,
                          int32_t id, void *data)
{
    (void)arg; (void)base; (void)data;
    switch (id) {
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "connecting to %s", HYDRA_WIFI_SSID);
            esp_wifi_connect();
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            s_connected = false;
            if (s_paused) {
                ESP_LOGI(TAG, "disconnected (paused for BLE)");
                break;
            }
            ESP_LOGW(TAG, "disconnected; retry in %d ms", RECONNECT_DELAY_MS);
            vTaskDelay(pdMS_TO_TICKS(RECONNECT_DELAY_MS));
            esp_wifi_connect();
            break;
        default:
            break;
    }
}

static void on_ip_event(void *arg, esp_event_base_t base,
                        int32_t id, void *data)
{
    (void)arg; (void)base;
    if (id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *evt = (ip_event_got_ip_t *)data;
        s_connected = true;
        ESP_LOGI(TAG, "got ip " IPSTR, IP2STR(&evt->ip_info.ip));
    }
}

static esp_err_t mdns_bring_up(void)
{
    esp_err_t err = mdns_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mdns_init failed: 0x%x", err);
        return err;
    }
    err = mdns_hostname_set(HYDRA_MDNS_HOSTNAME);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mdns_hostname_set failed: 0x%x", err);
        return err;
    }
    err = mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mdns_service_add failed: 0x%x", err);
    }
    ESP_LOGI(TAG, "mdns: %s.local", HYDRA_MDNS_HOSTNAME);
    return ESP_OK;
}

esp_err_t hydra_wifi_start(void)
{
    if (s_started) return ESP_OK;

    ESP_ERROR_CHECK(esp_netif_init());

    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;

    s_netif = esp_netif_create_default_wifi_sta();
    if (!s_netif) return ESP_FAIL;

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, on_ip_event, NULL, NULL));

    wifi_config_t sta = {0};
    strncpy((char *)sta.sta.ssid,     HYDRA_WIFI_SSID, sizeof sta.sta.ssid - 1);
    strncpy((char *)sta.sta.password, HYDRA_WIFI_PSK,  sizeof sta.sta.password - 1);
    sta.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    sta.sta.pmf_cfg.capable    = true;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta));
    ESP_ERROR_CHECK(esp_wifi_start());

    /* WiFi power-save mode time-slices the shared 2.4 GHz radio. On
     * ESP32-S3 with WiFi+BLE coex, PS transitions can blackhole BLE
     * CONNECT_REQ packets, manifesting as 8-second connect timeouts.
     * Pinning PS off lets BLE always win the radio when it asks. */
    esp_err_t ps_err = esp_wifi_set_ps(WIFI_PS_NONE);
    if (ps_err != ESP_OK) {
        ESP_LOGW(TAG, "wifi_set_ps NONE failed: 0x%x", ps_err);
    }

    mdns_bring_up();

    s_started = true;
    return ESP_OK;
}

esp_err_t hydra_wifi_pause(void)
{
    if (!s_started) return ESP_OK;
    s_paused = true;
    s_connected = false;
    /* Full stop, not just disconnect. The driver's background scan
     * activity contends for the shared 2.4 GHz radio enough to
     * blackhole BLE connect windows even when disassociated. */
    esp_err_t err = esp_wifi_stop();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_INIT) {
        ESP_LOGW(TAG, "wifi_stop failed: 0x%x", err);
    }
    return err;
}

esp_err_t hydra_wifi_resume(void)
{
    if (!s_started) return ESP_OK;
    s_paused = false;
    esp_err_t err = esp_wifi_start();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "wifi_start failed: 0x%x", err);
        return err;
    }
    /* esp_wifi_start triggers WIFI_EVENT_STA_START → handler calls
     * esp_wifi_connect itself, so no explicit connect call needed. */
    return ESP_OK;
}

#endif /* ESP_PLATFORM */
