#include "command_engine.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_log.h"
static const char *TAG = "command_engine";
#endif

static uint32_t g_seq = 0;

void command_engine_reset(void)
{
    g_seq = 0;
}

static void generate_command_id(char out[CMD_ID_LEN])
{
    ++g_seq;
    snprintf(out, CMD_ID_LEN, "cmd-%lu", (unsigned long)g_seq);
}

static ce_result_t build_state_for_kind(const ce_request_t *req,
                                        const registered_light_t *light,
                                        channel_state_t *out)
{
    switch (req->kind) {
        case CE_KIND_POWER_ON:
            return preset_expand(PRESET_ON, out) == 0
                ? CE_RESULT_ACCEPTED : CE_RESULT_INTERNAL_ERROR;

        case CE_KIND_POWER_OFF:
            return preset_expand(PRESET_OFF, out) == 0
                ? CE_RESULT_ACCEPTED : CE_RESULT_INTERNAL_ERROR;

        case CE_KIND_PRESET:
            if (req->preset == PRESET_NONE) return CE_RESULT_INVALID_COMMAND;
            return preset_expand(req->preset, out) == 0
                ? CE_RESULT_ACCEPTED : CE_RESULT_INVALID_COMMAND;

        case CE_KIND_SET_CHANNELS: {
            const channel_state_t *base = NULL;
            if (!req->replace && light) base = &light->last_state;
            int rc = channel_state_build(out, base, req->replace,
                                         req->channels, req->channel_count);
            if (rc != 0) {
                for (size_t i = 0; i < req->channel_count; ++i) {
                    if (!channel_model_by_name(req->channels[i].name)) return CE_RESULT_INVALID_CHANNEL;
                    if (!channel_value_is_valid(req->channels[i].value)) return CE_RESULT_INVALID_INTENSITY;
                }
                return CE_RESULT_INVALID_COMMAND;
            }
            return CE_RESULT_ACCEPTED;
        }

        case CE_KIND_RAMP:
            if (req->ramp_steps == 0) return CE_RESULT_INVALID_COMMAND;
            if (!channel_value_is_valid(req->ramp_from)) return CE_RESULT_INVALID_INTENSITY;
            if (!channel_value_is_valid(req->ramp_to))   return CE_RESULT_INVALID_INTENSITY;
            channel_state_zero(out);
            out->values[0] = req->ramp_from;
            return CE_RESULT_ACCEPTED;

        case CE_KIND_IDENTIFY:
            return preset_expand(PRESET_ON, out) == 0
                ? CE_RESULT_ACCEPTED : CE_RESULT_INTERNAL_ERROR;

        case CE_KIND_NONE:
        default:
            return CE_RESULT_INVALID_COMMAND;
    }
}

static cmd_type_t kind_to_cmd_type(ce_kind_t k)
{
    switch (k) {
        case CE_KIND_POWER_ON:
        case CE_KIND_POWER_OFF:
        case CE_KIND_PRESET:
        case CE_KIND_SET_CHANNELS: return CMD_TYPE_SET_CHANNELS;
        case CE_KIND_RAMP:         return CMD_TYPE_RAMP;
        case CE_KIND_IDENTIFY:     return CMD_TYPE_IDENTIFY;
        default:                   return CMD_TYPE_NONE;
    }
}

ce_result_t command_engine_submit(const ce_request_t *req,
                                  char out_command_id[CMD_ID_LEN])
{
    if (!req || !out_command_id) return CE_RESULT_INVALID_COMMAND;
    out_command_id[0] = '\0';

    if (req->target_id[0] == '\0') return CE_RESULT_INVALID_TARGET;
    const registered_light_t *light = light_registry_get(req->target_id);
    if (!light) return CE_RESULT_INVALID_TARGET;

    channel_state_t state;
    ce_result_t r = build_state_for_kind(req, light, &state);
    if (r != CE_RESULT_ACCEPTED) return r;

    pending_command_t cmd;
    memset(&cmd, 0, sizeof cmd);

    if (req->command_id[0] != '\0') {
        strncpy(cmd.command_id, req->command_id, CMD_ID_LEN - 1);
    } else {
        generate_command_id(cmd.command_id);
    }
    cmd.source            = req->source;
    cmd.type              = kind_to_cmd_type(req->kind);
    strncpy(cmd.light_id, req->target_id, LIGHT_ID_LEN - 1);
    cmd.timeout_ms        = req->command_timeout_ms ? req->command_timeout_ms : 30000;
    cmd.state             = state;
    cmd.scene_timeout_sec = req->scene_timeout_sec ? req->scene_timeout_sec : 60;
    cmd.ramp_from         = req->ramp_from;
    cmd.ramp_to           = req->ramp_to;
    cmd.ramp_duration_ms  = req->ramp_duration_ms;
    cmd.ramp_steps        = req->ramp_steps;

    int rc = cmd_queue_push(&cmd);
    if (rc != 0) return CE_RESULT_QUEUE_FULL;

    strncpy(out_command_id, cmd.command_id, CMD_ID_LEN - 1);
    out_command_id[CMD_ID_LEN - 1] = '\0';
    return CE_RESULT_ACCEPTED;
}

#ifdef ESP_PLATFORM
esp_err_t command_engine_init(void)
{
    command_engine_reset();
    ESP_LOGI(TAG, "init");
    return ESP_OK;
}
#endif
