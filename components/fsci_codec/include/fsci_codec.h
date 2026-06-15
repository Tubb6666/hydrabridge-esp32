#ifndef HYDRA_FSCI_CODEC_H
#define HYDRA_FSCI_CODEC_H

#include <stddef.h>
#include <stdint.h>

/* fsci_codec
 * ==========
 * FSCI (Freescale Serial Communications Interface) frame format used by
 * the myAI/Mobius BLE protocol. See docs/myai-ble-reverse-engineering.md
 * for the wire format derivation.
 *
 *   offset  size  meaning
 *   0       1     start marker  = 0x02
 *   1       1     op group
 *   2       1     op code
 *   3       2     message id    (little-endian)
 *   5       2     reserved flags
 *   7       2     payload length (little-endian)
 *   9       N     payload
 *   9+N     2     CRC16 over bytes [1 .. 8+N], little-endian
 *
 * Phase 1.1 ships the CRC primitive only. Frame builder (1.2) and
 * parser (1.3) follow.
 */

#define FSCI_START_MARKER             0x02
#define FSCI_HEADER_LEN_BYTES         9     /* start + og + oc + msgid + flags + len */
#define FSCI_TRAILER_LEN_BYTES        2     /* CRC16 LE */
#define FSCI_FRAME_OVERHEAD_BYTES     (FSCI_HEADER_LEN_BYTES + FSCI_TRAILER_LEN_BYTES)

/* Op groups from protocol constants observed in live captures. */
#define FSCI_OG_C2CI_REQUEST          0xde
#define FSCI_OG_C2CI_CONFIRM          0xdf

/* Op codes used for v1 control. */
#define FSCI_OC_LEGACY_GET_C2_ATTR    0x17
#define FSCI_OC_LEGACY_SET_C2_ATTR    0x18

/* CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, no input/output
 * reflection, xorout 0). The myAI protocol computes this over the FSCI
 * frame bytes starting at the op_group (offset 1) and ending at the
 * last payload byte (offset 8+N). The two-byte result is stored
 * little-endian after the payload.
 *
 * Pure C, no esp-idf dependency. Host-testable. */
uint16_t fsci_crc16(const uint8_t *data, size_t len);

/* Build a complete FSCI frame in `out`. Returns 0 on success or -1 if
 * `out_cap` is too small. On success, *out_len_written is set to the
 * total frame length:
 *
 *   FSCI_HEADER_LEN_BYTES + payload_len + FSCI_TRAILER_LEN_BYTES
 *
 * payload may be NULL when payload_len == 0. The reserved-flags field
 * is always written as 0x0000 -- the v1 protocol does not use the
 * group-id / no-respond bits. */
int fsci_build(uint8_t op_group,
               uint8_t op_code,
               uint16_t msg_id,
               const uint8_t *payload,
               size_t payload_len,
               uint8_t *out,
               size_t out_cap,
               size_t *out_len_written);

/* Parsed view over a complete FSCI frame. payload points into the
 * source buffer -- caller owns the lifetime. */
typedef struct {
    uint8_t        op_group;
    uint8_t        op_code;
    uint16_t       msg_id;
    const uint8_t *payload;
    size_t         payload_len;
} fsci_frame_t;

typedef enum {
    FSCI_PARSE_OK             = 0,
    FSCI_PARSE_BUF_TOO_SHORT  = -1,
    FSCI_PARSE_BAD_START      = -2,
    FSCI_PARSE_LEN_MISMATCH   = -3,
    FSCI_PARSE_BAD_CRC        = -4,
    FSCI_PARSE_NULL_ARG       = -5,
} fsci_parse_result_t;

/* Parse a complete FSCI frame. Validates start marker, declared
 * payload length matches the buffer, and CRC. On success the fields in
 * *out are populated and out->payload points into buf. */
int fsci_parse(const uint8_t *buf, size_t len, fsci_frame_t *out);

/* Reassembly buffer for fragmented BLE notifications. RX Data
 * fragments are appended; RX Final triggers a parse attempt. */
#define FSCI_REASSEMBLY_CAP 512
typedef struct {
    uint8_t buf[FSCI_REASSEMBLY_CAP];
    size_t  len;
} fsci_reassembly_t;

void fsci_reassembly_reset(fsci_reassembly_t *r);

/* Append a non-final RX Data fragment. Returns 0 on success or
 * FSCI_PARSE_BUF_TOO_SHORT (-1) if the buffer would overflow (also
 * resets the buffer in that case to avoid a partial frame jamming the
 * stream). */
int fsci_reassembly_append(fsci_reassembly_t *r, const uint8_t *data, size_t len);

/* Append the final RX Final fragment, then parse the accumulated
 * buffer. Resets the buffer afterwards regardless of parse outcome --
 * a bad frame should not block the next message. Returns
 * fsci_parse_result_t. */
int fsci_reassembly_finalize(fsci_reassembly_t *r,
                             const uint8_t *data, size_t len,
                             fsci_frame_t *out);

#ifdef ESP_PLATFORM
#include "esp_err.h"
esp_err_t fsci_codec_init(void);
#endif

#endif /* HYDRA_FSCI_CODEC_H */
