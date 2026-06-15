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

        case CE_KIND_INTENSITY_ADJUST: {
            /* Full intensity logic: start from current last_state (preserves exact current color mix),
             * add delta (pos for increase, neg for decrease) to ALL 9 channel intensities,
             * clamp [0,1000]. This works for arbitrary colors/profiles. Delta typically +/-50 or +/-100.
             * Uses last_state so it adjusts "the current scene" without needing full channels in the cmd. */
            if (!light) return CE_RESULT_INVALID_TARGET;
            *out = light->last_state;
            for (size_t i = 0; i < CHANNEL_COUNT; ++i) {
                int32_t v = (int32_t)out->values[i] + req->intensity_delta;
                if (v < 0) v = 0;
                if (v > 1000) v = 1000;
                out->values[i] = (uint16_t)v;
            }
            return CE_RESULT_ACCEPTED;
        }

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
        case CE_KIND_SET_CHANNELS:
        case CE_KIND_INTENSITY_ADJUST: return CMD_TYPE_SET_CHANNELS;
        case CE_KIND_RAMP:             return CMD_TYPE_RAMP;
        case CE_KIND_IDENTIFY:         return CMD_TYPE_IDENTIFY;
        default:                       return CMD_TYPE_NONE;
    }
}

static ce_result_t submit_to_light(const ce_request_t *req,
                                   const registered_light_t *light,
                                   char out_command_id[CMD_ID_LEN])
{
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

ce_result_t command_engine_submit(const ce_request_t *req,
                                  char out_command_id[CMD_ID_LEN])
{
    if (!req || !out_command_id) return CE_RESULT_INVALID_COMMAND;
    out_command_id[0] = '\0';

    if (req->target_id[0] == '\0') return CE_RESULT_INVALID_TARGET;
    if (req->target_type == CE_TARGET_GROUP) {
        const light_group_t *group = group_registry_get(req->target_id);
        if (!group || !group->enabled || group->member_count == 0) {
            return CE_RESULT_INVALID_TARGET;
        }

        ce_result_t first_error = CE_RESULT_ACCEPTED;
        size_t accepted = 0;
        for (uint8_t i = 0; i < group->member_count; ++i) {
            const registered_light_t *light = light_registry_get(group->light_ids[i]);
            if (!light || !light->enabled) {
                if (first_error == CE_RESULT_ACCEPTED) first_error = CE_RESULT_INVALID_TARGET;
                continue;
            }
            ce_request_t member_req = *req;
            member_req.target_type = CE_TARGET_LIGHT;
            strncpy(member_req.target_id, light->light_id, LIGHT_ID_LEN - 1);
            member_req.target_id[LIGHT_ID_LEN - 1] = '\0';
            char member_command_id[CMD_ID_LEN] = {0};
            ce_result_t r = submit_to_light(&member_req, light, member_command_id);
            if (r == CE_RESULT_ACCEPTED) {
                ++accepted;
                if (out_command_id[0] == '\0') {
                    strncpy(out_command_id, member_command_id, CMD_ID_LEN - 1);
                    out_command_id[CMD_ID_LEN - 1] = '\0';
                }
            } else if (first_error == CE_RESULT_ACCEPTED) {
                first_error = r;
            }
        }

        if (accepted == group->member_count) return CE_RESULT_ACCEPTED;
        if (accepted > 0) return CE_RESULT_PARTIAL_FAILURE;
        return first_error == CE_RESULT_ACCEPTED ? CE_RESULT_INVALID_TARGET : first_error;
    }

    const registered_light_t *light = light_registry_get(req->target_id);
    if (!light || !light->enabled) return CE_RESULT_INVALID_TARGET;
    return submit_to_light(req, light, out_command_id);
}

#ifdef ESP_PLATFORM
esp_err_t command_engine_init(void)
{
    command_engine_reset();
    ESP_LOGI(TAG, "init");
    return ESP_OK;
}
#endif
