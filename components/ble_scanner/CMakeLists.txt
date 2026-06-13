idf_component_register(
    SRCS "src/ble_scanner.c"
    INCLUDE_DIRS "include"
    REQUIRES light_registry
    PRIV_REQUIRES bt nvs_flash esp_event log
)
