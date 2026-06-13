/* Phase 1.3: parse the captured RX Final confirms and exercise the
 * reassembly buffer. */
#include "unity.h"
#include "all_tests.h"
#include "fsci_codec.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* 14-byte RX Final confirm: 9 header + 3 payload (status + 0xff 0xff) + 2 CRC. */
static const uint8_t RX_ON[] = {
    0x02, 0xdf, 0x18, 0x50, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0xff, 0xff, 0x59, 0xe6
};

static void test_parse_ok(void)
{
    fsci_frame_t f;
    int rc = fsci_parse(RX_ON, sizeof RX_ON, &f);
    TEST_ASSERT_EQUAL_INT(FSCI_PARSE_OK, rc);
    TEST_ASSERT_EQUAL_HEX8(0xdf, f.op_group);
    TEST_ASSERT_EQUAL_HEX8(0x18, f.op_code);
    TEST_ASSERT_EQUAL_HEX16(0x0050, f.msg_id);
    TEST_ASSERT_EQUAL_size_t(3, f.payload_len);
    TEST_ASSERT_NOT_NULL(f.payload);
    TEST_ASSERT_EQUAL_HEX8(0x00, f.payload[0]); /* status = success */
    TEST_ASSERT_EQUAL_HEX8(0xff, f.payload[1]);
    TEST_ASSERT_EQUAL_HEX8(0xff, f.payload[2]);
}

static void test_parse_bad_start(void)
{
    uint8_t bad[sizeof RX_ON];
    memcpy(bad, RX_ON, sizeof RX_ON);
    bad[0] = 0x03;
    fsci_frame_t f;
    TEST_ASSERT_EQUAL_INT(FSCI_PARSE_BAD_START, fsci_parse(bad, sizeof bad, &f));
}

static void test_parse_too_short(void)
{
    fsci_frame_t f;
    uint8_t buf[5] = {0x02, 0xdf, 0x18, 0x50, 0x00};
    TEST_ASSERT_EQUAL_INT(FSCI_PARSE_BUF_TOO_SHORT, fsci_parse(buf, sizeof buf, &f));
}

static void test_parse_len_mismatch(void)
{
    fsci_frame_t f;
    TEST_ASSERT_EQUAL_INT(FSCI_PARSE_LEN_MISMATCH, fsci_parse(RX_ON, sizeof RX_ON - 1, &f));
}

static void test_parse_bad_crc(void)
{
    uint8_t corrupt[sizeof RX_ON];
    memcpy(corrupt, RX_ON, sizeof RX_ON);
    corrupt[12] ^= 0x01;
    fsci_frame_t f;
    TEST_ASSERT_EQUAL_INT(FSCI_PARSE_BAD_CRC, fsci_parse(corrupt, sizeof corrupt, &f));
}

static void test_parse_null_args(void)
{
    fsci_frame_t f;
    TEST_ASSERT_EQUAL_INT(FSCI_PARSE_NULL_ARG, fsci_parse(NULL, 14, &f));
    TEST_ASSERT_EQUAL_INT(FSCI_PARSE_NULL_ARG, fsci_parse(RX_ON, sizeof RX_ON, NULL));
}

/* ---- reassembly ---- */

static void test_reassembly_single_fragment(void)
{
    fsci_reassembly_t r;
    fsci_reassembly_reset(&r);
    fsci_frame_t f;
    int rc = fsci_reassembly_finalize(&r, RX_ON, sizeof RX_ON, &f);
    TEST_ASSERT_EQUAL_INT(FSCI_PARSE_OK, rc);
    TEST_ASSERT_EQUAL_HEX16(0x0050, f.msg_id);
}

static void test_reassembly_two_fragments(void)
{
    fsci_reassembly_t r;
    fsci_reassembly_reset(&r);
    TEST_ASSERT_EQUAL_INT(FSCI_PARSE_OK, fsci_reassembly_append(&r, RX_ON, 5));
    fsci_frame_t f;
    int rc = fsci_reassembly_finalize(&r, RX_ON + 5, sizeof RX_ON - 5, &f);
    TEST_ASSERT_EQUAL_INT(FSCI_PARSE_OK, rc);
    TEST_ASSERT_EQUAL_HEX16(0x0050, f.msg_id);
    TEST_ASSERT_EQUAL_size_t(0, r.len);
}

static void test_reassembly_three_fragments(void)
{
    fsci_reassembly_t r;
    fsci_reassembly_reset(&r);
    TEST_ASSERT_EQUAL_INT(FSCI_PARSE_OK, fsci_reassembly_append(&r, RX_ON,      4));
    TEST_ASSERT_EQUAL_INT(FSCI_PARSE_OK, fsci_reassembly_append(&r, RX_ON + 4,  6));
    fsci_frame_t f;
    int rc = fsci_reassembly_finalize(&r, RX_ON + 10, sizeof RX_ON - 10, &f);
    TEST_ASSERT_EQUAL_INT(FSCI_PARSE_OK, rc);
}

static void test_reassembly_overflow_resets(void)
{
    fsci_reassembly_t r;
    fsci_reassembly_reset(&r);
    uint8_t junk[FSCI_REASSEMBLY_CAP + 1] = {0};
    int rc = fsci_reassembly_append(&r, junk, sizeof junk);
    TEST_ASSERT_EQUAL_INT(FSCI_PARSE_BUF_TOO_SHORT, rc);
    TEST_ASSERT_EQUAL_size_t(0, r.len);
    fsci_frame_t f;
    rc = fsci_reassembly_finalize(&r, RX_ON, sizeof RX_ON, &f);
    TEST_ASSERT_EQUAL_INT(FSCI_PARSE_OK, rc);
}

static void test_reassembly_bad_crc_propagates(void)
{
    uint8_t corrupt[sizeof RX_ON];
    memcpy(corrupt, RX_ON, sizeof RX_ON);
    corrupt[12] ^= 0x01;
    fsci_reassembly_t r;
    fsci_reassembly_reset(&r);
    fsci_frame_t f;
    int rc = fsci_reassembly_finalize(&r, corrupt, sizeof corrupt, &f);
    TEST_ASSERT_EQUAL_INT(FSCI_PARSE_BAD_CRC, rc);
    TEST_ASSERT_EQUAL_size_t(0, r.len);
}

void register_fsci_parser_tests(void)
{
    RUN_TEST(test_parse_ok);
    RUN_TEST(test_parse_bad_start);
    RUN_TEST(test_parse_too_short);
    RUN_TEST(test_parse_len_mismatch);
    RUN_TEST(test_parse_bad_crc);
    RUN_TEST(test_parse_null_args);
    RUN_TEST(test_reassembly_single_fragment);
    RUN_TEST(test_reassembly_two_fragments);
    RUN_TEST(test_reassembly_three_fragments);
    RUN_TEST(test_reassembly_overflow_resets);
    RUN_TEST(test_reassembly_bad_crc_propagates);
}
