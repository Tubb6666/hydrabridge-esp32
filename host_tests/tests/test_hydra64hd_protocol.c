/* Phase 1.5: byte-equal verification of hydra64hd_protocol payload
 * builders against captured TX frames in
 * docs/myai-ble-reverse-engineering.md.
 *
 * Strategy: extract the FSCI payload (bytes [9..len-3]) from each
 * captured TX frame, call the matching builder, and compare. */
#include "unity.h"
#include "all_tests.h"
#include "hydra64hd_protocol.h"
#include "channel_model.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* ---- SupportedColorChannels read (4-byte payload per capture) ---- */

static void test_build_get_chans_count1(void)
{
    /* Captured TX: 02 de 17 02 00 00 00 04 00 85 03 00 01 79 58
     * Payload (4 bytes): 85 03 00 01 */
    static const uint8_t expected[] = {0x85, 0x03, 0x00, 0x01};
    uint8_t out[8];
    size_t n = hydra64_build_supported_channels_read(1, out, sizeof out);
    TEST_ASSERT_EQUAL_size_t(sizeof expected, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, out, sizeof expected);
}

static void test_build_get_chans_count8(void)
{
    static const uint8_t expected[] = {0x85, 0x03, 0x00, 0x08};
    uint8_t out[8];
    size_t n = hydra64_build_supported_channels_read(8, out, sizeof out);
    TEST_ASSERT_EQUAL_size_t(sizeof expected, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, out, sizeof expected);
}

static void test_build_get_chans_count9(void)
{
    static const uint8_t expected[] = {0x85, 0x03, 0x00, 0x09};
    uint8_t out[8];
    size_t n = hydra64_build_supported_channels_read(9, out, sizeof out);
    TEST_ASSERT_EQUAL_size_t(sizeof expected, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, out, sizeof expected);
}

static void test_build_get_chans_buf_too_small(void)
{
    uint8_t out[3];
    TEST_ASSERT_EQUAL_size_t(0, hydra64_build_supported_channels_read(9, out, sizeof out));
}

static void test_build_get_chans_null_out(void)
{
    TEST_ASSERT_EQUAL_size_t(0, hydra64_build_supported_channels_read(9, NULL, 100));
}

/* ---- LiveDemoScene write payload (56-byte FSCI inner body) ---- */

static const uint8_t TX_ON[] = {
    0x02, 0xde, 0x18, 0x50, 0x00, 0x00, 0x00, 0x38, 0x00, 0x97, 0x01, 0x00, 0x01, 0x33, 0x01, 0x00,
    0x00, 0x00, 0x01, 0x00, 0x3c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xe8, 0x03, 0x10, 0xe8, 0x03, 0x11, 0xe8, 0x03, 0x19,
    0xe8, 0x03, 0x17, 0xe8, 0x03, 0x15, 0xe8, 0x03, 0x13, 0xe8, 0x03, 0x12, 0xe8, 0x03, 0x1e, 0xe8,
    0x03, 0xac, 0x80,
};
static const uint8_t TX_OFF[] = {
    0x02, 0xde, 0x18, 0x51, 0x00, 0x00, 0x00, 0x38, 0x00, 0x97, 0x01, 0x00, 0x01, 0x33, 0x01, 0x00,
    0x00, 0x00, 0x01, 0x00, 0x3c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x10, 0x00, 0x00, 0x11, 0x00, 0x00, 0x19,
    0x00, 0x00, 0x17, 0x00, 0x00, 0x15, 0x00, 0x00, 0x13, 0x00, 0x00, 0x12, 0x00, 0x00, 0x1e, 0x00,
    0x00, 0x03, 0xca,
};
static const uint8_t TX_LBM[] = {
    0x02, 0xde, 0x18, 0x53, 0x00, 0x00, 0x00, 0x38, 0x00, 0x97, 0x01, 0x00, 0x01, 0x33, 0x01, 0x00,
    0x00, 0x00, 0x01, 0x00, 0x3c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x14, 0x00, 0x10, 0x00, 0x00, 0x11, 0x00, 0x00, 0x19,
    0x00, 0x00, 0x17, 0x00, 0x00, 0x15, 0x00, 0x00, 0x13, 0x00, 0x00, 0x12, 0x00, 0x00, 0x1e, 0x14,
    0x00, 0x90, 0x04,
};
static const uint8_t TX_MOON[] = {
    0x02, 0xde, 0x18, 0x54, 0x00, 0x00, 0x00, 0x38, 0x00, 0x97, 0x01, 0x00, 0x01, 0x33, 0x01, 0x00,
    0x00, 0x00, 0x01, 0x00, 0x3c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xe8, 0x03, 0x10, 0x00, 0x00, 0x11, 0x00, 0x00, 0x19,
    0x00, 0x00, 0x17, 0x00, 0x00, 0x15, 0x00, 0x00, 0x13, 0x00, 0x00, 0x12, 0x00, 0x00, 0x1e, 0x32,
    0x00, 0x46, 0x99,
};

#define FSCI_HEADER 9
#define FSCI_TAIL   2

static void compare_payload(const uint8_t *captured, size_t captured_len,
                            const channel_state_t *state, uint16_t timeout)
{
    size_t plen = captured_len - FSCI_HEADER - FSCI_TAIL;
    TEST_ASSERT_EQUAL_size_t(HYDRA64_SET_LDS_PAYLOAD_BYTES, plen);

    uint8_t built[HYDRA64_SET_LDS_PAYLOAD_BYTES];
    size_t n = hydra64_build_live_demo_scene_write(state, timeout, built, sizeof built);
    TEST_ASSERT_EQUAL_size_t(HYDRA64_SET_LDS_PAYLOAD_BYTES, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(&captured[FSCI_HEADER], built, plen);
}

static void test_build_lds_on(void)
{
    channel_state_t s;
    for (int i = 0; i < CHANNEL_COUNT; ++i) s.values[i] = 1000;
    compare_payload(TX_ON, sizeof TX_ON, &s, 60);
}

static void test_build_lds_off(void)
{
    channel_state_t s;
    channel_state_zero(&s);
    compare_payload(TX_OFF, sizeof TX_OFF, &s, 60);
}

static void test_build_lds_low_bright_moon(void)
{
    channel_kv_t kv[] = {{ "brightness", 20 }, { "moonlight", 20 }};
    channel_state_t s;
    TEST_ASSERT_EQUAL_INT(0, channel_state_build(&s, NULL, true, kv, 2));
    compare_payload(TX_LBM, sizeof TX_LBM, &s, 60);
}

static void test_build_lds_moonlight(void)
{
    channel_kv_t kv[] = {{ "brightness", 1000 }, { "moonlight", 50 }};
    channel_state_t s;
    TEST_ASSERT_EQUAL_INT(0, channel_state_build(&s, NULL, true, kv, 2));
    compare_payload(TX_MOON, sizeof TX_MOON, &s, 60);
}

static void test_build_lds_buf_too_small(void)
{
    channel_state_t s;
    channel_state_zero(&s);
    uint8_t out[HYDRA64_SET_LDS_PAYLOAD_BYTES - 1];
    TEST_ASSERT_EQUAL_size_t(0, hydra64_build_live_demo_scene_write(&s, 60, out, sizeof out));
}

static void test_build_lds_null_state(void)
{
    uint8_t out[HYDRA64_SET_LDS_PAYLOAD_BYTES];
    TEST_ASSERT_EQUAL_size_t(0, hydra64_build_live_demo_scene_write(NULL, 60, out, sizeof out));
}

static void test_build_lds_timeout_zero(void)
{
    channel_state_t s;
    channel_state_zero(&s);
    uint8_t out[HYDRA64_SET_LDS_PAYLOAD_BYTES];
    size_t n = hydra64_build_live_demo_scene_write(&s, 0, out, sizeof out);
    TEST_ASSERT_EQUAL_size_t(HYDRA64_SET_LDS_PAYLOAD_BYTES, n);
    /* Wrapper(5) + primitive(4) + scene_id(2) = offsets 11,12 are the timeout bytes. */
    TEST_ASSERT_EQUAL_HEX8(0x00, out[11]);
    TEST_ASSERT_EQUAL_HEX8(0x00, out[12]);
}

void register_hydra64hd_tests(void)
{
    RUN_TEST(test_build_get_chans_count1);
    RUN_TEST(test_build_get_chans_count8);
    RUN_TEST(test_build_get_chans_count9);
    RUN_TEST(test_build_get_chans_buf_too_small);
    RUN_TEST(test_build_get_chans_null_out);
    RUN_TEST(test_build_lds_on);
    RUN_TEST(test_build_lds_off);
    RUN_TEST(test_build_lds_low_bright_moon);
    RUN_TEST(test_build_lds_moonlight);
    RUN_TEST(test_build_lds_buf_too_small);
    RUN_TEST(test_build_lds_null_state);
    RUN_TEST(test_build_lds_timeout_zero);
}
