idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "."
    REQUIRES
        nvs_flash
        esp_event
        esp_wifi
        bt
        driver
        fsci_codec
        channel_model
        hydra64hd_protocol
        ble_light_client
        modbus_interface
        mqtt_bridge
        web_ui
        config_store
        event_log
        command_engine
        ota_update
        preset_engine
        light_registry
        command_queue
        ble_scanner
        ha_discovery
)
