#ifndef HYDRA_MODBUS_INTERFACE_H
#define HYDRA_MODBUS_INTERFACE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "modbus_registers.h"

/* modbus_interface: in-memory holding-register store + snapshot-on-
 * command_seq-increment logic. Pure C so it's host-testable. */

#define MODBUS_STORE_REG_COUNT 2400

void modbus_store_reset(void);
void     modbus_store_set(uint16_t addr, uint16_t value);
uint16_t modbus_store_get(uint16_t addr);
int modbus_store_read(uint16_t addr, uint16_t count, uint16_t *out);
int modbus_store_write(uint16_t addr, uint16_t count, const uint16_t *values);
void modbus_store_process_pending(void);
void modbus_store_refresh_status_mirrors(void);

#ifdef ESP_PLATFORM
#include "esp_err.h"
esp_err_t modbus_interface_init(void);
esp_err_t modbus_interface_reconfigure(void);
esp_err_t modbus_slave_driver_start(void);
esp_err_t modbus_slave_driver_stop(void);
bool modbus_slave_driver_is_running(void);
uint16_t *modbus_store_raw_array(void);
#endif

#endif /* HYDRA_MODBUS_INTERFACE_H */
