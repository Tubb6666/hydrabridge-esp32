#include "hydra64hd_protocol.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_log.h"
static const char *TAG = "hydra64hd_protocol";
#endif

static inline void put_u16_le(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
}

size_t hydra64_build_supported_channels_read(uint8_t count,
                                             uint8_t *out, size_t out_cap)
{
    if (!out) return 0;
    if (out_cap < HYDRA64_GET_CHANS_PAYLOAD_BYTES) return 0;
    put_u16_le(&out[0], HYDRA64_ATTR_SUPPORTED_COLOR_CHANS);
    out[2] = 0;
    out[3] = count;
    return HYDRA64_GET_CHANS_PAYLOAD_BYTES;
}

size_t hydra64_build_live_demo_scene_write(const channel_state_t *state,
                                           uint16_t timeout_seconds,
                                           uint8_t *out, size_t out_cap)
{
    if (!state || !out) return 0;
    if (out_cap < HYDRA64_SET_LDS_PAYLOAD_BYTES) return 0;

    put_u16_le(&out[0], HYDRA64_ATTR_LIVE_DEMO_SCENE);
    out[2] = 0;
    out[3] = 1;
    out[4] = HYDRA64_LIVE_DEMO_SCENE_BYTES;

    uint8_t *lds = &out[5];
    size_t k = 0;

    lds[k++] = HYDRA64_PRIMITIVE_VISUAL_V1_PREFIX;
    lds[k++] = 0; lds[k++] = 0; lds[k++] = 0;

    put_u16_le(&lds[k], HYDRA64_SCENE_ID); k += 2;
    put_u16_le(&lds[k], timeout_seconds);  k += 2;

    memset(&lds[k], 0, HYDRA64_SCENE_NAME_BYTES);
    k += HYDRA64_SCENE_NAME_BYTES;

    const channel_def_t *defs = channel_model_all();
    for (int i = 0; i < CHANNEL_COUNT; ++i) {
        lds[k++] = defs[i].visual_id;
        put_u16_le(&lds[k], state->values[i]);
        k += 2;
    }

    return HYDRA64_SET_LDS_PAYLOAD_BYTES;
}

#ifdef ESP_PLATFORM
esp_err_t hydra64hd_protocol_init(void)
{
    ESP_LOGI(TAG, "init (LiveDemoScene attr=0x%04x, %d bytes)",
             HYDRA64_ATTR_LIVE_DEMO_SCENE, HYDRA64_LIVE_DEMO_SCENE_BYTES);
    return ESP_OK;
}
#endif
