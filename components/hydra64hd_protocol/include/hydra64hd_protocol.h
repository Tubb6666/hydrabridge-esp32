#ifndef HYDRA_HYDRA64HD_PROTOCOL_H
#define HYDRA_HYDRA64HD_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#include "channel_model.h"

/* hydra64hd_protocol
 * ==================
 * Builds the FSCI payload bytes for the two control primitives we use
 * in v1. These functions return the INNER body that the caller wraps
 * via fsci_build(); no FSCI header / CRC here.
 *
 *   - hydra64_build_supported_channels_read(): GetC2Attr 0x17 for
 *     attribute 0x0385 (SupportedColorChannels). 4-byte payload.
 *   - hydra64_build_live_demo_scene_write(): SetC2Attr 0x18 for
 *     attribute 0x0197 (LiveDemoScene). 55-byte payload =
 *     4-byte wrapper + 51-byte LiveDemoScene value.
 */

#define HYDRA64_ATTR_LIVE_DEMO_SCENE         0x0197
#define HYDRA64_ATTR_SUPPORTED_COLOR_CHANS   0x0385

#define HYDRA64_OPCODE_LEGACY_GET            0x17
#define HYDRA64_OPCODE_LEGACY_SET            0x18

#define HYDRA64_LIVE_DEMO_SCENE_BYTES        51
#define HYDRA64_SCENE_NAME_BYTES             16
#define HYDRA64_PRIMITIVE_VISUAL_V1_PREFIX   0x01
#define HYDRA64_SCENE_ID                     1

#define HYDRA64_GET_CHANS_PAYLOAD_BYTES      4
/* SetC2Attr wrapper for LiveDemoScene is 5 bytes (attr LE + start +
 * count + elem_len) followed by the 51-byte value. Total payload =
 * 5 + 51 = 56 = 0x38, matching the captured payload_len byte. */
#define HYDRA64_SET_LDS_PAYLOAD_BYTES        (5 + HYDRA64_LIVE_DEMO_SCENE_BYTES)

size_t hydra64_build_supported_channels_read(uint8_t count,
                                             uint8_t *out, size_t out_cap);

size_t hydra64_build_live_demo_scene_write(const channel_state_t *state,
                                           uint16_t timeout_seconds,
                                           uint8_t *out, size_t out_cap);

#ifdef ESP_PLATFORM
#include "esp_err.h"
esp_err_t hydra64hd_protocol_init(void);
#endif

#endif /* HYDRA_HYDRA64HD_PROTOCOL_H */
