idf_component_register(
    SRCS "src/command_queue.c"
    INCLUDE_DIRS "include"
    REQUIRES channel_model light_registry
    PRIV_REQUIRES esp_timer log
)
