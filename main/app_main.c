#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "ble_light_client.h"
#include "command_engine.h"
#include "config_store.h"
#include "event_log.h"
#include "modbus_interface.h"
#include "mqtt_bridge.h"
#include "ota_update.h"
#include "web_ui.h"

static const char *TAG = "hydra_app";

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_LOGI(TAG, "Hydra 64HD controller starting");

    event_log_init();

    /* Run OTA init early -- if this boot is a freshly-flashed image we
     * want to cancel the pending rollback before anything else can
     * crash. */
    ESP_ERROR_CHECK(ota_update_init());

    ESP_ERROR_CHECK(config_store_init());
    ESP_ERROR_CHECK(command_engine_init());

    /* Modbus is the reliable local control path and should start before optional network services. */
    ESP_ERROR_CHECK(modbus_interface_init());

    ESP_ERROR_CHECK(ble_light_client_init());
    ESP_ERROR_CHECK(ble_light_client_start());

    /* MQTT and web UI are optional based on persisted config. */
    ESP_ERROR_CHECK(mqtt_bridge_init());
    ESP_ERROR_CHECK(web_ui_init());

    ESP_LOGI(TAG, "Hydra 64HD controller initialized");
}
