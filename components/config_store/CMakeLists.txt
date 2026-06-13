idf_component_register(
    SRCS "src/config_store.c"
         "src/config_store_defaults.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES nvs_flash log
)
