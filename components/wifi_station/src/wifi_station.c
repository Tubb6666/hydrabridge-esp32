#ifdef ESP_PLATFORM

#include "wifi_station.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "config_store.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/ip4_addr.h"
#include "mdns.h"

static const char *TAG = "wifi_station";

#define RECONNECT_DELAY_MS        5000
#define FALLBACK_AFTER_RETRIES    12

static bool            s_started;
static bool            s_connected;
static bool            s_paused;
static bool            s_ap_active;
static bool            s_mdns_started;
static uint8_t         s_retry_count;
static config_wifi_t   s_wifi_cfg;
static esp_netif_t    *s_sta_netif;
static esp_netif_t    *s_ap_netif;
static esp_timer_handle_t s_reconnect_timer;

bool hydra_wifi_is_connected(void) { return s_connected; }
bool hydra_wifi_ap_is_active(void) { return s_ap_active; }

hydra_wifi_mode_t hydra_wifi_mode(void)
{
    if (s_connected && s_ap_active) return HYDRA_WIFI_MODE_APSTA;
    if (s_connected) return HYDRA_WIFI_MODE_STA;
    if (s_ap_active) return HYDRA_WIFI_MODE_AP;
    return HYDRA_WIFI_MODE_OFF;
}

static void reconnect_timer_cb(void *arg)
{
    (void)arg;
    if (!s_started || s_paused || !s_wifi_cfg.enabled || s_wifi_cfg.ssid[0] == '\0') return;
    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK && err != ESP_ERR_WIFI_CONN) {
        ESP_LOGW(TAG, "esp_wifi_connect failed: 0x%x", err);
    }
}

static void schedule_reconnect(void)
{
    if (!s_reconnect_timer) return;
    esp_timer_stop(s_reconnect_timer);
    esp_timer_start_once(s_reconnect_timer, RECONNECT_DELAY_MS * 1000ULL);
}

static esp_err_t set_ap_ip(void)
{
    if (!s_ap_netif) return ESP_ERR_INVALID_STATE;

    esp_netif_ip_info_t ip;
    memset(&ip, 0, sizeof ip);
    IP4_ADDR(&ip.ip, 192, 168, 1, 10);
    IP4_ADDR(&ip.gw, 192, 168, 1, 10);
    IP4_ADDR(&ip.netmask, 255, 255, 255, 0);

    esp_err_t err = esp_netif_dhcps_stop(s_ap_netif);
    if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) return err;
    err = esp_netif_set_ip_info(s_ap_netif, &ip);
    if (err != ESP_OK) return err;
    err = esp_netif_dhcps_start(s_ap_netif);
    if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED) return err;
    return ESP_OK;
}

static esp_err_t apply_wifi_mode(void)
{
    wifi_mode_t mode = WIFI_MODE_NULL;
    bool sta_enabled = s_wifi_cfg.enabled && s_wifi_cfg.ssid[0] != '\0';
    if (sta_enabled && s_ap_active) mode = WIFI_MODE_APSTA;
    else if (sta_enabled) mode = WIFI_MODE_STA;
    else if (s_ap_active) mode = WIFI_MODE_AP;

    ESP_ERROR_CHECK(esp_wifi_set_mode(mode));
    return ESP_OK;
}

static esp_err_t start_setup_ap(void)
{
    if (!s_wifi_cfg.ap_fallback_enabled || s_ap_active) return ESP_OK;

    wifi_config_t ap = {0};
    const char *ssid = s_wifi_cfg.ap_ssid[0] ? s_wifi_cfg.ap_ssid : "HydraBridge-Setup";
    const char *psk = s_wifi_cfg.ap_password;
    strncpy((char *)ap.ap.ssid, ssid, sizeof ap.ap.ssid - 1);
    ap.ap.ssid_len = strlen(ssid);
    ap.ap.channel = 6;
    ap.ap.max_connection = 4;
    if (psk[0] && strlen(psk) >= 8) {
        strncpy((char *)ap.ap.password, psk, sizeof ap.ap.password - 1);
        ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    } else {
        ap.ap.authmode = WIFI_AUTH_OPEN;
    }

    s_ap_active = true;
    ESP_ERROR_CHECK(apply_wifi_mode());
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    ESP_ERROR_CHECK(set_ap_ip());
    ESP_LOGW(TAG, "setup AP active: ssid=%s url=http://192.168.1.10/", ssid);
    return ESP_OK;
}

static esp_err_t stop_setup_ap(void)
{
    if (!s_ap_active) return ESP_OK;
    s_ap_active = false;
    ESP_ERROR_CHECK(apply_wifi_mode());
    ESP_LOGI(TAG, "setup AP stopped");
    return ESP_OK;
}

static esp_err_t configure_sta(void)
{
    wifi_config_t sta = {0};
    strncpy((char *)sta.sta.ssid, s_wifi_cfg.ssid, sizeof sta.sta.ssid - 1);
    strncpy((char *)sta.sta.password, s_wifi_cfg.password, sizeof sta.sta.password - 1);
    sta.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    sta.sta.pmf_cfg.capable = true;
    return esp_wifi_set_config(WIFI_IF_STA, &sta);
}

static void on_wifi_event(void *arg, esp_event_base_t base,
                          int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)data;
    switch (id) {
        case WIFI_EVENT_STA_START:
            if (s_wifi_cfg.enabled && s_wifi_cfg.ssid[0]) {
                ESP_LOGI(TAG, "connecting to configured WiFi SSID");
                esp_wifi_connect();
            }
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            s_connected = false;
            if (s_paused) {
                ESP_LOGI(TAG, "disconnected (paused for BLE)");
                break;
            }
            if (++s_retry_count >= FALLBACK_AFTER_RETRIES) {
                start_setup_ap();
            }
            ESP_LOGW(TAG, "wifi disconnected; retry %u in %d ms",
                     (unsigned)s_retry_count, RECONNECT_DELAY_MS);
            schedule_reconnect();
            break;
        default:
            break;
    }
}

static void on_ip_event(void *arg, esp_event_base_t base,
                        int32_t id, void *data)
{
    (void)arg;
    (void)base;
    if (id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *evt = (ip_event_got_ip_t *)data;
        s_connected = true;
        s_retry_count = 0;
        ESP_LOGI(TAG, "got ip " IPSTR, IP2STR(&evt->ip_info.ip));
        if (s_ap_active && s_wifi_cfg.ap_fallback_enabled) {
            stop_setup_ap();
        }
    }
}

static esp_err_t mdns_bring_up(void)
{
    if (s_mdns_started) return ESP_OK;
    esp_err_t err = mdns_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mdns_init failed: 0x%x", err);
        return err;
    }
    err = mdns_hostname_set("hydrabridge");
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mdns_hostname_set failed: 0x%x", err);
        return err;
    }
    err = mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mdns_service_add failed: 0x%x", err);
    }
    s_mdns_started = true;
    ESP_LOGI(TAG, "mdns: hydrabridge.local");
    return ESP_OK;
}

esp_err_t hydra_wifi_start(void)
{
    if (s_started) return ESP_OK;

    config_store_load_wifi(&s_wifi_cfg);

    ESP_ERROR_CHECK(esp_netif_init());

    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;

    s_sta_netif = esp_netif_create_default_wifi_sta();
    s_ap_netif = esp_netif_create_default_wifi_ap();
    if (!s_sta_netif || !s_ap_netif) return ESP_FAIL;

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, on_ip_event, NULL, NULL));

    const esp_timer_create_args_t timer_args = {
        .callback = reconnect_timer_cb,
        .name = "wifi_reconnect",
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_reconnect_timer));

    bool has_sta = s_wifi_cfg.enabled && s_wifi_cfg.ssid[0] != '\0';
    s_ap_active = !has_sta && s_wifi_cfg.ap_fallback_enabled;
    ESP_ERROR_CHECK(apply_wifi_mode());
    if (has_sta) ESP_ERROR_CHECK(configure_sta());
    if (s_ap_active) {
        wifi_config_t ap = {0};
        const char *ssid = s_wifi_cfg.ap_ssid[0] ? s_wifi_cfg.ap_ssid : "HydraBridge-Setup";
        const char *psk = s_wifi_cfg.ap_password;
        strncpy((char *)ap.ap.ssid, ssid, sizeof ap.ap.ssid - 1);
        ap.ap.ssid_len = strlen(ssid);
        ap.ap.channel = 6;
        ap.ap.max_connection = 4;
        if (psk[0] && strlen(psk) >= 8) {
            strncpy((char *)ap.ap.password, psk, sizeof ap.ap.password - 1);
            ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
        } else {
            ap.ap.authmode = WIFI_AUTH_OPEN;
        }
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
        ESP_ERROR_CHECK(set_ap_ip());
    }
    ESP_ERROR_CHECK(esp_wifi_start());

    esp_err_t ps_err = esp_wifi_set_ps(WIFI_PS_NONE);
    if (ps_err != ESP_OK) {
        ESP_LOGW(TAG, "wifi_set_ps NONE failed: 0x%x", ps_err);
    }

    mdns_bring_up();

    s_started = true;
    if (s_ap_active) {
        ESP_LOGW(TAG, "setup AP active: ssid=%s url=http://192.168.1.10/",
                 s_wifi_cfg.ap_ssid[0] ? s_wifi_cfg.ap_ssid : "HydraBridge-Setup");
    }
    return ESP_OK;
}

esp_err_t hydra_wifi_reconfigure(void)
{
    if (!s_started) return hydra_wifi_start();

    config_store_load_wifi(&s_wifi_cfg);
    s_connected = false;
    s_retry_count = 0;
    esp_timer_stop(s_reconnect_timer);
    esp_wifi_disconnect();

    bool has_sta = s_wifi_cfg.enabled && s_wifi_cfg.ssid[0] != '\0';
    s_ap_active = false;
    ESP_ERROR_CHECK(apply_wifi_mode());
    if (has_sta) {
        ESP_ERROR_CHECK(configure_sta());
    }
    if (!has_sta && s_wifi_cfg.ap_fallback_enabled) {
        ESP_ERROR_CHECK(start_setup_ap());
    }
    if (has_sta) {
        esp_err_t err = esp_wifi_connect();
        if (err != ESP_OK && err != ESP_ERR_WIFI_CONN) return err;
    }
    return ESP_OK;
}

esp_err_t hydra_wifi_pause(void)
{
    if (!s_started) return ESP_OK;
    s_paused = true;
    s_connected = false;
    esp_timer_stop(s_reconnect_timer);
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
    return ESP_OK;
}

#endif /* ESP_PLATFORM */
