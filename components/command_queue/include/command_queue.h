#ifndef HYDRA_COMMAND_QUEUE_H
#define HYDRA_COMMAND_QUEUE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "channel_model.h"
#include "light_registry.h"

/* command_queue
 * =============
 * Per-light bounded FIFOs of pending commands. The BLE worker drains
 * one queue at a time.
 *
 * Coalescing rule: if a CMD_TYPE_SET_CHANNELS is enqueued while another
 * SET_CHANNELS for the same light is already in the queue, the older
 * one is REPLACED. RAMP and IDENTIFY commands never coalesce.
 */

#define CMD_QUEUE_DEPTH       8
#define CMD_ID_LEN            33

typedef enum {
    CMD_SOURCE_SYSTEM = 0,
    CMD_SOURCE_MODBUS = 1,
    CMD_SOURCE_MQTT   = 2,
    CMD_SOURCE_WEB    = 3,
} cmd_source_t;

typedef enum {
    CMD_TYPE_NONE         = 0,
    CMD_TYPE_SET_CHANNELS = 1,  /* coalescable */
    CMD_TYPE_RAMP         = 2,  /* not coalescable */
    CMD_TYPE_IDENTIFY     = 3,  /* not coalescable */
} cmd_type_t;

typedef struct {
    char            command_id[CMD_ID_LEN];
    cmd_source_t    source;
    cmd_type_t      type;
    char            light_id[LIGHT_ID_LEN];
    uint64_t        enqueue_ms;
    uint32_t        timeout_ms;
    uint8_t         retry_count;
    channel_state_t state;
    uint16_t        scene_timeout_sec;
    uint16_t        ramp_from;
    uint16_t        ramp_to;
    uint32_t        ramp_duration_ms;
    uint16_t        ramp_steps;
} pending_command_t;

void cmd_queue_reset(void);
int  cmd_queue_push(const pending_command_t *cmd);
int  cmd_queue_pop(const char *light_id, pending_command_t *out);
int  cmd_queue_peek(const char *light_id, pending_command_t *out);
size_t cmd_queue_depth(const char *light_id);
void cmd_queue_set_clock_fn(uint64_t (*fn)(void));
size_t cmd_queue_expire(void);

#ifdef ESP_PLATFORM
#include "esp_err.h"
esp_err_t cmd_queue_init(void);
#endif

#endif /* HYDRA_COMMAND_QUEUE_H */
