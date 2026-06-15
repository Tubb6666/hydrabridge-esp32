#ifndef HOST_TESTS_ALL_TESTS_H
#define HOST_TESTS_ALL_TESTS_H

void register_smoke_tests(void);
void register_event_log_tests(void);
void register_config_store_tests(void);
void register_crc16_tests(void);
void register_fsci_builder_tests(void);
void register_fsci_parser_tests(void);
void register_channel_model_tests(void);
void register_hydra64hd_tests(void);
void register_preset_engine_tests(void);
void register_light_registry_tests(void);
void register_ble_scanner_tests(void);
void register_command_queue_tests(void);
void register_command_engine_tests(void);
void register_modbus_interface_tests(void);
void register_mqtt_parser_tests(void);

#endif /* HOST_TESTS_ALL_TESTS_H */
