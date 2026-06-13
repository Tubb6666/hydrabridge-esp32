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

int fsci_parse(const uint8_t *buf, size_t len, fsci_frame_t *out)
{
    if (!buf || !out) return FSCI_PARSE_NULL_ARG;
    if (len < FSCI_FRAME_OVERHEAD_BYTES) return FSCI_PARSE_BUF_TOO_SHORT;
    if (buf[0] != FSCI_START_MARKER) return FSCI_PARSE_BAD_START;

    uint16_t declared_plen = (uint16_t)buf[7] | ((uint16_t)buf[8] << 8);
    size_t expected_total = FSCI_FRAME_OVERHEAD_BYTES + declared_plen;
    if (len != expected_total) return FSCI_PARSE_LEN_MISMATCH;

    /* CRC over bytes [1 .. 8+N]. */
    uint16_t crc = fsci_crc16(&buf[1], 8 + declared_plen);
    uint8_t stored_lo = buf[FSCI_HEADER_LEN_BYTES + declared_plen + 0];
    uint8_t stored_hi = buf[FSCI_HEADER_LEN_BYTES + declared_plen + 1];
    uint16_t stored = (uint16_t)stored_lo | ((uint16_t)stored_hi << 8);
    if (stored != crc) return FSCI_PARSE_BAD_CRC;

    out->op_group    = buf[1];
    out->op_code     = buf[2];
    out->msg_id      = (uint16_t)buf[3] | ((uint16_t)buf[4] << 8);
    out->payload     = declared_plen ? &buf[FSCI_HEADER_LEN_BYTES] : NULL;
    out->payload_len = declared_plen;
    return FSCI_PARSE_OK;
}

void fsci_reassembly_reset(fsci_reassembly_t *r)
{
    if (r) r->len = 0;
}

int fsci_reassembly_append(fsci_reassembly_t *r, const uint8_t *data, size_t len)
{
    if (!r || (len > 0 && !data)) return FSCI_PARSE_NULL_ARG;
    if (r->len + len > FSCI_REASSEMBLY_CAP) {
        r->len = 0;
        return FSCI_PARSE_BUF_TOO_SHORT;
    }
    if (len) {
        for (size_t i = 0; i < len; ++i) r->buf[r->len + i] = data[i];
        r->len += len;
    }
    return FSCI_PARSE_OK;
}

int fsci_reassembly_finalize(fsci_reassembly_t *r,
                             const uint8_t *data, size_t len,
                             fsci_frame_t *out)
{
    if (!r) return FSCI_PARSE_NULL_ARG;
    int rc = fsci_reassembly_append(r, data, len);
    if (rc != FSCI_PARSE_OK) return rc;
    int parse_rc = fsci_parse(r->buf, r->len, out);
    /* The reassembly buffer is in a stable storage area, so it's
     * safe to keep out->payload pointing into it after this call --
     * but DO reset the length so the next message starts clean. */
    r->len = 0;
    return parse_rc;
}

#ifdef ESP_PLATFORM
esp_err_t fsci_codec_init(void)
{
    ESP_LOGI(TAG, "init (poly=0x1021 init=0xFFFF)");
    return ESP_OK;
}
#endif
