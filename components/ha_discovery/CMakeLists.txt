idf_component_register(
    SRCS "src/ha_discovery.c"
    INCLUDE_DIRS "include"
    REQUIRES light_registry channel_model preset_engine
    PRIV_REQUIRES log
)
