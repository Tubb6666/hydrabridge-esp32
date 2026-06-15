#ifndef HYDRA_COMMAND_ENGINE_H
#define HYDRA_COMMAND_ENGINE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "channel_model.h"
#include "command_queue.h"
#include "light_registry.h"
#include "preset_engine.h"

/* command_engine: normalizes commands from any input surface and
 * enqueues them on command_queue. Result codes align with the Modbus
 * last_result_code enum in spec L855-873. */

typedef enum {
    CE_RESULT_ACCEPTED          = 1,
    CE_RESULT_SUCCESS           = 3,
    CE_RESULT_PARTIAL_FAILURE   = 4,
    CE_RESULT_INVALID_COMMAND   = 10,
    CE_RESULT_INVALID_TARGET    = 11,
    CE_RESULT_INVALID_CHANNEL   = 12,
    CE_RESULT_INVALID_INTENSITY = 13,
    CE_RESULT_BUSY              = 14,
    CE_RESULT_QUEUE_FULL        = 15,
    CE_RESULT_INTERNAL_ERROR    = 30,
} ce_result_t;

typedef enum {
    CE_TARGET_LIGHT = 0,
    CE_TARGET_GROUP = 1,
} ce_target_type_t;

typedef enum {
    CE_KIND_NONE         = 0,
    CE_KIND_POWER_ON     = 1,
    CE_KIND_POWER_OFF    = 2,
    CE_KIND_PRESET       = 3,
    CE_KIND_SET_CHANNELS = 4,
    CE_KIND_RAMP         = 5,
    CE_KIND_IDENTIFY     = 6,
    CE_KIND_INTENSITY_ADJUST = 7,  /* +delta or -delta to all channel intensities (preserves color mix) */
} ce_kind_t;

typedef struct {
    cmd_source_t source;
    char         command_id[CMD_ID_LEN];
    char         target_id[LIGHT_ID_LEN];
    ce_target_type_t target_type;
    ce_kind_t    kind;

    preset_id_t  preset;

    channel_kv_t channels[CHANNEL_COUNT];
    size_t       channel_count;
    bool         replace;

    uint16_t     scene_timeout_sec;
    uint32_t     command_timeout_ms;

    uint16_t     ramp_from;
    uint16_t     ramp_to;
    uint32_t     ramp_duration_ms;
    uint16_t     ramp_steps;

    int16_t      intensity_delta;  /* for CE_KIND_INTENSITY_ADJUST: e.g. +50 or -100; applied to all 9 channels, clamped */
} ce_request_t;

ce_result_t command_engine_submit(const ce_request_t *req,
                                  char out_command_id[CMD_ID_LEN]);

void command_engine_reset(void);

#ifdef ESP_PLATFORM
#include "esp_err.h"
esp_err_t command_engine_init(void);
#endif

#endif /* HYDRA_COMMAND_ENGINE_H */
