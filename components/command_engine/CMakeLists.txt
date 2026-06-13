idf_component_register(
    SRCS "src/command_engine.c"
    INCLUDE_DIRS "include"
    REQUIRES channel_model preset_engine light_registry command_queue
    PRIV_REQUIRES log
)
