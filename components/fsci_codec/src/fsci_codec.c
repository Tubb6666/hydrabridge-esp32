#include "fsci_codec.h"

#include <stddef.h>
#include <stdint.h>

#ifdef ESP_PLATFORM
#include "esp_log.h"
static const char *TAG = "fsci_codec";
#endif

/* CRC-16/CCITT-FALSE bit-by-bit. The packet sizes we deal with are tiny
 * (~70 bytes per write, even smaller per confirm) so the loop cost is
 * negligible compared to BLE write latency. A table-based version can
 * replace this later without API changes if profiling demands it. */
uint16_t fsci_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    if (!data) return crc;
    for (size_t i = 0; i < len; ++i) {
        crc ^= ((uint16_t)data[i]) << 8;
        for (int bit = 0; bit < 8; ++bit) {
            if (crc & 0x8000) {
                crc = (uint16_t)((crc << 1) ^ 0x1021);
            } else {
                crc = (uint16_t)(crc << 1);
            }
        }
    }
    return crc;
}

int fsci_build(uint8_t op_group,
               uint8_t op_code,
               uint16_t msg_id,
               const uint8_t *payload,
               size_t payload_len,
               uint8_t *out,
               size_t out_cap,
               size_t *out_len_written)
{
    if (!out) return -1;
    size_t total = FSCI_HEADER_LEN_BYTES + payload_len + FSCI_TRAILER_LEN_BYTES;
    if (out_cap < total) return -1;

    /* Header. */
    out[0] = FSCI_START_MARKER;
    out[1] = op_group;
    out[2] = op_code;
    out[3] = (uint8_t)(msg_id & 0xFF);
    out[4] = (uint8_t)((msg_id >> 8) & 0xFF);
    out[5] = 0;
    out[6] = 0;
    out[7] = (uint8_t)(payload_len & 0xFF);
    out[8] = (uint8_t)((payload_len >> 8) & 0xFF);

    /* Payload. */
    if (payload && payload_len) {
        for (size_t i = 0; i < payload_len; ++i) {
            out[FSCI_HEADER_LEN_BYTES + i] = payload[i];
        }
    }

    /* CRC over bytes [1 .. 8+N]. */
    size_t crc_scope_len = 8 + payload_len; /* bytes counted from offset 1 */
    uint16_t crc = fsci_crc16(&out[1], crc_scope_len);
    out[FSCI_HEADER_LEN_BYTES + payload_len + 0] = (uint8_t)(crc & 0xFF);
    out[FSCI_HEADER_LEN_BYTES + payload_len + 1] = (uint8_t)((crc >> 8) & 0xFF);

    if (out_len_written) *out_len_written = total;
    return 0;
}

#ifdef ESP_PLATFORM
esp_err_t fsci_codec_init(void)
{
    ESP_LOGI(TAG, "init (poly=0x1021 init=0xFFFF)");
    return ESP_OK;
}
#endif
