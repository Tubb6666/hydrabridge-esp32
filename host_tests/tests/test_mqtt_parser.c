/* Phase 5.1b: mqtt_bridge JSON parser tests against every payload
 * shape in docs/esp32-hydra64hd-controller-plan.md L1138-L1208. */
#include "unity.h"
#include "all_tests.h"
#include "mqtt_bridge.h"
#include "command_engine.h"
#include "preset_engine.h"

#include <string.h>
#include <stdbool.h>

static void test_power_on(void)
{
    const char *json = "{\"power\":\"on\",\"timeout\":60}";
    ce_request_t r;
    TEST_ASSERT_EQUAL_INT(0, mqtt_parse_light_command(json, "L1", &r));
    TEST_ASSERT_EQUAL_INT(CE_KIND_POWER_ON, r.kind);
    TEST_ASSERT_EQUAL_INT(CMD_SOURCE_MQTT, r.source);
    TEST_ASSERT_EQUAL_STRING("L1", r.target_id);
    TEST_ASSERT_EQUAL_UINT16(60, r.scene_timeout_sec);
}

static void test_power_off(void)
{
    const char *json = "{\"power\":\"off\",\"timeout\":60}";
    ce_request_t r;
    TEST_ASSERT_EQUAL_INT(0, mqtt_parse_light_command(json, "L1", &r));
    TEST_ASSERT_EQUAL_INT(CE_KIND_POWER_OFF, r.kind);
}

static void test_power_unknown_value(void)
{
    const char *json = "{\"power\":\"sideways\",\"timeout\":60}";
    ce_request_t r;
    TEST_ASSERT_EQUAL_INT(-1, mqtt_parse_light_command(json, "L1", &r));
}

static void test_preset_moonlight(void)
{
    const char *json = "{\"preset\":\"moonlight\",\"timeout\":60}";
    ce_request_t r;
    TEST_ASSERT_EQUAL_INT(0, mqtt_parse_light_command(json, "L1", &r));
    TEST_ASSERT_EQUAL_INT(CE_KIND_PRESET, r.kind);
    TEST_ASSERT_EQUAL_INT(PRESET_MOONLIGHT, r.preset);
}

static void test_preset_blue_moonlight(void)
{
    const char *json = "{\"preset\":\"blue_moonlight\",\"timeout\":60}";
    ce_request_t r;
    TEST_ASSERT_EQUAL_INT(0, mqtt_parse_light_command(json, "L1", &r));
    TEST_ASSERT_EQUAL_INT(PRESET_BLUE_MOONLIGHT, r.preset);
}

static void test_preset_unknown_name(void)
{
    const char *json = "{\"preset\":\"weeknight_dim\",\"timeout\":60}";
    ce_request_t r;
    TEST_ASSERT_EQUAL_INT(-1, mqtt_parse_light_command(json, "L1", &r));
}

static void test_set_channels(void)
{
    const char *json =
        "{\"replace\":true,\"timeout\":60,\"channels\":"
        "{\"brightness\":1000,\"coolwhite\":0,\"blue\":80,\"deepred\":0,"
        "\"violet\":40,\"uv\":0,\"green\":0,\"royalblue\":120,\"moonlight\":50}}";
    ce_request_t r;
    TEST_ASSERT_EQUAL_INT(0, mqtt_parse_light_command(json, "L1", &r));
    TEST_ASSERT_EQUAL_INT(CE_KIND_SET_CHANNELS, r.kind);
    TEST_ASSERT_TRUE(r.replace);
    TEST_ASSERT_EQUAL_size_t(9, r.channel_count);

    bool found_brightness = false, found_moon = false;
    for (size_t i = 0; i < r.channel_count; ++i) {
        if (strcmp(r.channels[i].name, "brightness") == 0) {
            TEST_ASSERT_EQUAL_UINT16(1000, r.channels[i].value);
            found_brightness = true;
        }
        if (strcmp(r.channels[i].name, "moonlight") == 0) {
            TEST_ASSERT_EQUAL_UINT16(50, r.channels[i].value);
            found_moon = true;
        }
    }
    TEST_ASSERT_TRUE(found_brightness);
    TEST_ASSERT_TRUE(found_moon);
}

static void test_set_channels_partial_merge(void)
{
    const char *json = "{\"channels\":{\"brightness\":900}}";
    ce_request_t r;
    TEST_ASSERT_EQUAL_INT(0, mqtt_parse_light_command(json, "L1", &r));
    TEST_ASSERT_TRUE(r.replace);
    TEST_ASSERT_EQUAL_size_t(1, r.channel_count);
    TEST_ASSERT_EQUAL_STRING("brightness", r.channels[0].name);
    TEST_ASSERT_EQUAL_UINT16(900, r.channels[0].value);
}

static void test_set_channels_replace_false(void)
{
    const char *json = "{\"replace\":false,\"channels\":{\"blue\":200}}";
    ce_request_t r;
    TEST_ASSERT_EQUAL_INT(0, mqtt_parse_light_command(json, "L1", &r));
    TEST_ASSERT_FALSE(r.replace);
}

static void test_set_channels_unknown_name_rejected(void)
{
    const char *json = "{\"channels\":{\"bogus_channel\":500}}";
    ce_request_t r;
    TEST_ASSERT_EQUAL_INT(-1, mqtt_parse_light_command(json, "L1", &r));
}

static void test_ramp(void)
{
    const char *json =
        "{\"ramp\":{\"from\":0,\"to\":1000,\"duration_ms\":8000,\"steps\":20},"
        "\"timeout\":60}";
    ce_request_t r;
    TEST_ASSERT_EQUAL_INT(0, mqtt_parse_light_command(json, "L1", &r));
    TEST_ASSERT_EQUAL_INT(CE_KIND_RAMP, r.kind);
    TEST_ASSERT_EQUAL_UINT16(0,    r.ramp_from);
    TEST_ASSERT_EQUAL_UINT16(1000, r.ramp_to);
    TEST_ASSERT_EQUAL_UINT32(8000, r.ramp_duration_ms);
    TEST_ASSERT_EQUAL_UINT16(20,   r.ramp_steps);
}

static void test_ramp_missing_field(void)
{
    const char *json = "{\"ramp\":{\"from\":0,\"to\":1000,\"steps\":20}}";
    ce_request_t r;
    TEST_ASSERT_EQUAL_INT(-1, mqtt_parse_light_command(json, "L1", &r));
}

static void test_command_id_preserved(void)
{
    const char *json = "{\"command_id\":\"user-supplied-id-123\",\"power\":\"on\"}";
    ce_request_t r;
    TEST_ASSERT_EQUAL_INT(0, mqtt_parse_light_command(json, "L1", &r));
    TEST_ASSERT_EQUAL_STRING("user-supplied-id-123", r.command_id);
}

static void test_invalid_json(void)
{
    ce_request_t r;
    TEST_ASSERT_EQUAL_INT(-1, mqtt_parse_light_command("not json", "L1", &r));
    TEST_ASSERT_EQUAL_INT(-1, mqtt_parse_light_command("{",        "L1", &r));
    TEST_ASSERT_EQUAL_INT(-1, mqtt_parse_light_command("[1,2,3]",  "L1", &r));
}

static void test_empty_object_no_command(void)
{
    const char *json = "{}";
    ce_request_t r;
    TEST_ASSERT_EQUAL_INT(-1, mqtt_parse_light_command(json, "L1", &r));
}

static void test_null_args(void)
{
    ce_request_t r;
    TEST_ASSERT_EQUAL_INT(-1, mqtt_parse_light_command(NULL, "L1", &r));
    TEST_ASSERT_EQUAL_INT(-1, mqtt_parse_light_command("{}", NULL, &r));
    TEST_ASSERT_EQUAL_INT(-1, mqtt_parse_light_command("{}", "L1", NULL));
}

static void test_priority_ramp_beats_preset(void)
{
    const char *json =
        "{\"preset\":\"moonlight\","
        " \"ramp\":{\"from\":0,\"to\":1000,\"duration_ms\":5000,\"steps\":10}}";
    ce_request_t r;
    TEST_ASSERT_EQUAL_INT(0, mqtt_parse_light_command(json, "L1", &r));
    TEST_ASSERT_EQUAL_INT(CE_KIND_RAMP, r.kind);
}

void register_mqtt_parser_tests(void)
{
    RUN_TEST(test_power_on);
    RUN_TEST(test_power_off);
    RUN_TEST(test_power_unknown_value);
    RUN_TEST(test_preset_moonlight);
    RUN_TEST(test_preset_blue_moonlight);
    RUN_TEST(test_preset_unknown_name);
    RUN_TEST(test_set_channels);
    RUN_TEST(test_set_channels_partial_merge);
    RUN_TEST(test_set_channels_replace_false);
    RUN_TEST(test_set_channels_unknown_name_rejected);
    RUN_TEST(test_ramp);
    RUN_TEST(test_ramp_missing_field);
    RUN_TEST(test_command_id_preserved);
    RUN_TEST(test_invalid_json);
    RUN_TEST(test_empty_object_no_command);
    RUN_TEST(test_null_args);
    RUN_TEST(test_priority_ramp_beats_preset);
}
