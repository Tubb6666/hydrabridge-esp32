idf_component_register(
    SRCS "src/ble_light_client.c"
    INCLUDE_DIRS "include"
    REQUIRES command_queue light_registry channel_model
    PRIV_REQUIRES bt nvs_flash esp_event log fsci_codec hydra64hd_protocol
                  command_engine event_log ble_scanner
)
