idf_component_register(
    SRCS "src/wifi_station.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_wifi esp_netif esp_event esp_timer config_store log
                  espressif__mdns
)
