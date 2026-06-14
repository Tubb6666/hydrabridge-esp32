#include "esp_coexist.h"
#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "ble_light_client.h"
#include "command_engine.h"
#include "config_store.h"
#include "event_log.h"
#include "light_registry.h"
#include "modbus_interface.h"
#include "mqtt_bridge.h"
#include "ota_update.h"
#include "web_ui.h"
#include "wifi_station.h"

/* DIAGNOSTIC: define HYDRA_DIAG_BLE_ONLY to skip WiFi+web_ui and run an
 * auto on/off cycle 10 s after boot. */
/* #define HYDRA_DIAG_BLE_ONLY 1 */

/* DIAGNOSTIC: define HYDRA_AUTO_TEST to keep WiFi+web_ui running and
 * also auto-issue an on/off cycle. Use to verify the WiFi+BLE coex
 * tuning hands-off via monitor log. */
/* #define HYDRA_AUTO_TEST 1 */

#if defined(HYDRA_DIAG_BLE_ONLY) || defined(HYDRA_AUTO_TEST)
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

static const char *TAG = "hydra_app";

#if defined(HYDRA_DIAG_BLE_ONLY) || defined(HYDRA_AUTO_TEST)
static void diag_test_task(void *arg)
{
    (void)arg;
    ESP_LOGW(TAG, "DIAG: waiting 15 s for stacks to settle");
    vTaskDelay(pdMS_TO_TICKS(15000));

    registered_light_t l = {0};
    strncpy(l.light_id,     "diag", LIGHT_ID_LEN - 1);
    strncpy(l.display_name, "Diag", LIGHT_NAME_LEN - 1);
    strncpy(l.serial,       "9Q9B0BE6D2R1EB", LIGHT_SERIAL_LEN - 1);
    /* Hydra MAC 1C:BC:EC:0B:E6:D2 — wire-order is reverse so the
     * registry stores it MSB-first. */
    l.ble_addr[0] = 0x1C; l.ble_addr[1] = 0xBC; l.ble_addr[2] = 0xEC;
    l.ble_addr[3] = 0x0B; l.ble_addr[4] = 0xE6; l.ble_addr[5] = 0xD2;
    l.ble_addr_type = BLE_ADDR_PUBLIC;
    l.model         = 335;
    l.enabled       = true;
    int rc = light_registry_add(&l);
    ESP_LOGW(TAG, "DIAG: registry_add rc=%d count=%u", rc, (unsigned)light_registry_count());

    for (int cycle = 0;; ++cycle) {
        ce_request_t req = {0};
        req.source              = CMD_SOURCE_WEB;
        req.kind                = (cycle % 2 == 0) ? CE_KIND_POWER_ON : CE_KIND_POWER_OFF;
        req.scene_timeout_sec   = 60;
        req.command_timeout_ms  = 30000;
        strncpy(req.target_id, "diag", LIGHT_ID_LEN - 1);

        char cmd_id[CMD_ID_LEN] = {0};
        ce_result_t res = command_engine_submit(&req, cmd_id);
        ESP_LOGW(TAG, "DIAG cycle %d kind=%d submit=%d cmd_id=%s",
                 cycle, (int)req.kind, (int)res, cmd_id);

        vTaskDelay(pdMS_TO_TICKS(8000));
    }
}
#endif

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_LOGI(TAG, "Hydra 64HD controller starting");

    /* ESP32-S3 shares one 2.4 GHz radio between WiFi and BLE. The
     * default coex preference favors WiFi, which starves BLE central
     * connect-initiation windows and causes ble_gap_connect to time
     * out (status 13). Must be set before either stack inits. */
    esp_err_t coex_err = esp_coex_preference_set(ESP_COEX_PREFER_BT);
    if (coex_err != ESP_OK) {
        ESP_LOGW(TAG, "esp_coex_preference_set(BT) failed: 0x%x", coex_err);
    }

    event_log_init();

    /* Run OTA init early -- if this boot is a freshly-flashed image we
     * want to cancel the pending rollback before anything else can
     * crash. */
    ESP_ERROR_CHECK(ota_update_init());

    ESP_ERROR_CHECK(config_store_init());
    ESP_ERROR_CHECK(light_registry_init());
    ESP_ERROR_CHECK(command_engine_init());

#ifndef HYDRA_DIAG_BLE_ONLY
    /* Modbus is the reliable local control path and should start before optional network services. */
    ESP_ERROR_CHECK(modbus_interface_init());
#endif

    ESP_ERROR_CHECK(ble_light_client_init());
    ESP_ERROR_CHECK(ble_light_client_start());

#ifdef HYDRA_DIAG_BLE_ONLY
    ESP_LOGW(TAG, "DIAG MODE: WiFi+web_ui skipped; auto on/off cycle in 15 s");
    xTaskCreate(diag_test_task, "diag_test", 4096, NULL, 4, NULL);
#else
    /* MQTT and web UI are optional based on persisted config. */
    ESP_ERROR_CHECK(mqtt_bridge_init());

    /* WiFi must come up before the HTTP server so the netif exists. */
    ESP_ERROR_CHECK(hydra_wifi_start());

    ESP_ERROR_CHECK(web_ui_init());

#  ifdef HYDRA_AUTO_TEST
    ESP_LOGW(TAG, "AUTO TEST: WiFi+web_ui live; auto on/off cycle in 15 s");
    xTaskCreate(diag_test_task, "diag_test", 4096, NULL, 4, NULL);
#  endif
#endif

    ESP_LOGI(TAG, "Hydra 64HD controller initialized");
}
