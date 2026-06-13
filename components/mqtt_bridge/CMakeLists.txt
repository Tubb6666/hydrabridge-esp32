idf_component_register(
    SRCS "src/mqtt_bridge.c"
    INCLUDE_DIRS "include"
    REQUIRES command_engine channel_model preset_engine json
    PRIV_REQUIRES log
)
