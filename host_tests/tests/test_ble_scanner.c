#include "unity.h"
#include "all_tests.h"
#include "ble_scanner.h"

static void test_parse_captured_hydra64hd_manufacturer_payload(void)
{
    const uint8_t payload[] = {
        0x02, 0x02, 0x4f, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00,
        0xff, 0xff, 0x48, 0x59, 0x44, 0x52, 0x41, 0x36, 0x34,
        0x45, 0x58, 0x4d, 0x50, 0x4c, 0x30, 0x31
    };
    myai_manuf_t out;

    TEST_ASSERT_EQUAL_INT(0, ble_scanner_parse_manuf(payload, sizeof payload, &out));
    TEST_ASSERT_TRUE(out.parsed_ok);
    TEST_ASSERT_EQUAL_UINT8(2, out.version);
    TEST_ASSERT_EQUAL_UINT8(2, out.flags);
    TEST_ASSERT_EQUAL_UINT16(335, out.model);
    TEST_ASSERT_EQUAL_STRING("HYDRA64EXMPL01", out.serial);
}

static void test_parse_short_v2_pairing_payload_keeps_structure(void)
{
    const uint8_t payload[] = {0x02, 0x0a, 0x4f, 0x01};
    myai_manuf_t out;

    TEST_ASSERT_EQUAL_INT(0, ble_scanner_parse_manuf(payload, sizeof payload, &out));
    TEST_ASSERT_TRUE(out.parsed_ok);
    TEST_ASSERT_EQUAL_UINT8(2, out.version);
    TEST_ASSERT_EQUAL_UINT8(0x0a, out.flags);
    TEST_ASSERT_EQUAL_UINT16(335, out.model);
}

static void test_parse_rejects_unknown_manufacturer_version(void)
{
    const uint8_t payload[] = {0x03, 0x02, 0x4f, 0x01};
    myai_manuf_t out;

    TEST_ASSERT_EQUAL_INT(-1, ble_scanner_parse_manuf(payload, sizeof payload, &out));
}

void register_ble_scanner_tests(void)
{
    RUN_TEST(test_parse_captured_hydra64hd_manufacturer_payload);
    RUN_TEST(test_parse_short_v2_pairing_payload_keeps_structure);
    RUN_TEST(test_parse_rejects_unknown_manufacturer_version);
}
