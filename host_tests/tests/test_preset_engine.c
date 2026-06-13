/* Phase 1.6: preset_engine tests pin every built-in preset to the
 * exact channel values in docs/esp32-hydra64hd-controller-plan.md
 * "Preset System" (L661-L733). */
#include "unity.h"
#include "all_tests.h"
#include "preset_engine.h"
#include "channel_model.h"

#include <string.h>

/* Channel index helpers -- order must match channel_model_all(). */
#define IDX_BRIGHT  0
#define IDX_CW      1
#define IDX_BLUE    2
#define IDX_DR      3
#define IDX_VIOLET  4
#define IDX_UV      5
#define IDX_GREEN   6
#define IDX_RBLUE   7
#define IDX_MOON    8

static void test_preset_off(void)
{
    channel_state_t s;
    TEST_ASSERT_EQUAL_INT(0, preset_expand(PRESET_OFF, &s));
    for (int i = 0; i < CHANNEL_COUNT; ++i) {
        TEST_ASSERT_EQUAL_UINT16(0, s.values[i]);
    }
}

static void test_preset_on(void)
{
    channel_state_t s;
    TEST_ASSERT_EQUAL_INT(0, preset_expand(PRESET_ON, &s));
    for (int i = 0; i < CHANNEL_COUNT; ++i) {
        TEST_ASSERT_EQUAL_UINT16(1000, s.values[i]);
    }
}

static void test_preset_moonlight(void)
{
    channel_state_t s;
    TEST_ASSERT_EQUAL_INT(0, preset_expand(PRESET_MOONLIGHT, &s));
    TEST_ASSERT_EQUAL_UINT16(1000, s.values[IDX_BRIGHT]);
    TEST_ASSERT_EQUAL_UINT16(0,    s.values[IDX_CW]);
    TEST_ASSERT_EQUAL_UINT16(0,    s.values[IDX_BLUE]);
    TEST_ASSERT_EQUAL_UINT16(0,    s.values[IDX_DR]);
    TEST_ASSERT_EQUAL_UINT16(0,    s.values[IDX_VIOLET]);
    TEST_ASSERT_EQUAL_UINT16(0,    s.values[IDX_UV]);
    TEST_ASSERT_EQUAL_UINT16(0,    s.values[IDX_GREEN]);
    TEST_ASSERT_EQUAL_UINT16(0,    s.values[IDX_RBLUE]);
    TEST_ASSERT_EQUAL_UINT16(50,   s.values[IDX_MOON]);
}

static void test_preset_blue_moonlight(void)
{
    channel_state_t s;
    TEST_ASSERT_EQUAL_INT(0, preset_expand(PRESET_BLUE_MOONLIGHT, &s));
    TEST_ASSERT_EQUAL_UINT16(1000, s.values[IDX_BRIGHT]);
    TEST_ASSERT_EQUAL_UINT16(0,    s.values[IDX_CW]);
    TEST_ASSERT_EQUAL_UINT16(40,   s.values[IDX_BLUE]);
    TEST_ASSERT_EQUAL_UINT16(0,    s.values[IDX_DR]);
    TEST_ASSERT_EQUAL_UINT16(20,   s.values[IDX_VIOLET]);
    TEST_ASSERT_EQUAL_UINT16(0,    s.values[IDX_UV]);
    TEST_ASSERT_EQUAL_UINT16(0,    s.values[IDX_GREEN]);
    TEST_ASSERT_EQUAL_UINT16(80,   s.values[IDX_RBLUE]);
    TEST_ASSERT_EQUAL_UINT16(0,    s.values[IDX_MOON]);
}

static void test_preset_test_25(void)
{
    channel_state_t s;
    TEST_ASSERT_EQUAL_INT(0, preset_expand(PRESET_TEST_25, &s));
    TEST_ASSERT_EQUAL_UINT16(1000, s.values[IDX_BRIGHT]);
    for (int i = IDX_CW; i < CHANNEL_COUNT; ++i) {
        TEST_ASSERT_EQUAL_UINT16(250, s.values[i]);
    }
}

static void test_preset_unknown_returns_error(void)
{
    channel_state_t s;
    memset(&s, 0xAB, sizeof s);
    channel_state_t snapshot = s;
    TEST_ASSERT_EQUAL_INT(-1, preset_expand(PRESET_NONE, &s));
    TEST_ASSERT_EQUAL_INT(-1, preset_expand((preset_id_t)99, &s));
    TEST_ASSERT_EQUAL_MEMORY(&snapshot, &s, sizeof s);
}

static void test_preset_null_out(void)
{
    TEST_ASSERT_EQUAL_INT(-1, preset_expand(PRESET_ON, NULL));
}

static void test_preset_name(void)
{
    TEST_ASSERT_EQUAL_STRING("off",            preset_name(PRESET_OFF));
    TEST_ASSERT_EQUAL_STRING("on",             preset_name(PRESET_ON));
    TEST_ASSERT_EQUAL_STRING("moonlight",      preset_name(PRESET_MOONLIGHT));
    TEST_ASSERT_EQUAL_STRING("blue_moonlight", preset_name(PRESET_BLUE_MOONLIGHT));
    TEST_ASSERT_EQUAL_STRING("test_25",        preset_name(PRESET_TEST_25));
    TEST_ASSERT_NULL(preset_name(PRESET_NONE));
    TEST_ASSERT_NULL(preset_name((preset_id_t)99));
}

static void test_preset_by_name(void)
{
    TEST_ASSERT_EQUAL_INT(PRESET_OFF,            preset_by_name("off"));
    TEST_ASSERT_EQUAL_INT(PRESET_ON,             preset_by_name("on"));
    TEST_ASSERT_EQUAL_INT(PRESET_MOONLIGHT,      preset_by_name("moonlight"));
    TEST_ASSERT_EQUAL_INT(PRESET_BLUE_MOONLIGHT, preset_by_name("blue_moonlight"));
    TEST_ASSERT_EQUAL_INT(PRESET_TEST_25,        preset_by_name("test_25"));
    TEST_ASSERT_EQUAL_INT(PRESET_NONE,           preset_by_name("nope"));
    TEST_ASSERT_EQUAL_INT(PRESET_NONE,           preset_by_name(""));
    TEST_ASSERT_EQUAL_INT(PRESET_NONE,           preset_by_name(NULL));
    TEST_ASSERT_EQUAL_INT(PRESET_NONE,           preset_by_name("ON"));
}

static void test_preset_all_returns_5(void)
{
    size_t count = 0;
    const preset_info_t *all = preset_all(&count);
    TEST_ASSERT_EQUAL_size_t(PRESET_COUNT, count);
    TEST_ASSERT_NOT_NULL(all);
    for (size_t i = 0; i < count; ++i) {
        TEST_ASSERT_NOT_EQUAL_INT(PRESET_NONE, all[i].id);
        TEST_ASSERT_NOT_NULL(all[i].name);
        TEST_ASSERT_TRUE(strlen(all[i].name) > 0);
    }
}

void register_preset_engine_tests(void)
{
    RUN_TEST(test_preset_off);
    RUN_TEST(test_preset_on);
    RUN_TEST(test_preset_moonlight);
    RUN_TEST(test_preset_blue_moonlight);
    RUN_TEST(test_preset_test_25);
    RUN_TEST(test_preset_unknown_returns_error);
    RUN_TEST(test_preset_null_out);
    RUN_TEST(test_preset_name);
    RUN_TEST(test_preset_by_name);
    RUN_TEST(test_preset_all_returns_5);
}
