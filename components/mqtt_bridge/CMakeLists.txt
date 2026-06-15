idf_component_register(
    SRCS "src/mqtt_bridge.c"
    INCLUDE_DIRS "include"
    REQUIRES command_engine channel_model preset_engine json
    PRIV_REQUIRES log mqtt config_store light_registry ha_discovery modbus_interface
)
