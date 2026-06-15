/* Phase 3.3b: command_engine end-to-end tests. */
#include "unity.h"
#include "all_tests.h"
#include "command_engine.h"
#include "command_queue.h"
#include "light_registry.h"
#include "preset_engine.h"

#include <string.h>

static void setup(void)
{
    light_registry_reset();
    cmd_queue_reset();
    command_engine_reset();

    registered_light_t l;
    memset(&l, 0, sizeof l);
    strncpy(l.light_id, "L1", LIGHT_ID_LEN - 1);
    strncpy(l.display_name, "Test", LIGHT_NAME_LEN - 1);
    strncpy(l.serial, "S1", LIGHT_SERIAL_LEN - 1);
    l.model = 335;
    l.enabled = true;
    light_registry_add(&l);
}

static void add_second_light_and_group(void)
{
    registered_light_t l;
    memset(&l, 0, sizeof l);
    strncpy(l.light_id, "L2", LIGHT_ID_LEN - 1);
    strncpy(l.display_name, "Test 2", LIGHT_NAME_LEN - 1);
    strncpy(l.serial, "S2", LIGHT_SERIAL_LEN - 1);
    l.model = 335;
    l.enabled = true;
    light_registry_add(&l);

    light_group_t g;
    memset(&g, 0, sizeof g);
    strncpy(g.group_id, "G1", GROUP_ID_LEN - 1);
    strncpy(g.display_name, "Group", LIGHT_NAME_LEN - 1);
    strncpy(g.light_ids[0], "L1", LIGHT_ID_LEN - 1);
    strncpy(g.light_ids[1], "L2", LIGHT_ID_LEN - 1);
    g.member_count = 2;
    g.enabled = true;
    group_registry_add(&g);
}

static void make_req(ce_request_t *r, ce_kind_t kind, const char *target)
{
    memset(r, 0, sizeof *r);
    r->source = CMD_SOURCE_MQTT;
    strncpy(r->target_id, target, LIGHT_ID_LEN - 1);
    r->kind = kind;
}

static void test_unknown_target(void)
{
    setup();
    ce_request_t r;
    make_req(&r, CE_KIND_POWER_ON, "DOES_NOT_EXIST");
    char id[CMD_ID_LEN];
    TEST_ASSERT_EQUAL_INT(CE_RESULT_INVALID_TARGET, command_engine_submit(&r, id));
}

static void test_empty_target(void)
{
    setup();
    ce_request_t r;
    make_req(&r, CE_KIND_POWER_ON, "");
    char id[CMD_ID_LEN];
    TEST_ASSERT_EQUAL_INT(CE_RESULT_INVALID_TARGET, command_engine_submit(&r, id));
}

static void test_power_on_enqueues_preset_on(void)
{
    setup();
    ce_request_t r;
    make_req(&r, CE_KIND_POWER_ON, "L1");
    char id[CMD_ID_LEN];
    TEST_ASSERT_EQUAL_INT(CE_RESULT_ACCEPTED, command_engine_submit(&r, id));
    TEST_ASSERT_NOT_EQUAL_size_t(0, strlen(id));
    TEST_ASSERT_EQUAL_size_t(1, cmd_queue_depth("L1"));

    pending_command_t cmd;
    cmd_queue_pop("L1", &cmd);
    TEST_ASSERT_EQUAL_INT(CMD_TYPE_SET_CHANNELS, cmd.type);
    for (int i = 0; i < CHANNEL_COUNT; ++i) {
        TEST_ASSERT_EQUAL_UINT16(1000, cmd.state.values[i]);
    }
    TEST_ASSERT_EQUAL_UINT16(60, cmd.scene_timeout_sec);
    TEST_ASSERT_EQUAL_UINT32(30000, cmd.timeout_ms);
}

static void test_power_off_enqueues_preset_off(void)
{
    setup();
    ce_request_t r;
    make_req(&r, CE_KIND_POWER_OFF, "L1");
    char id[CMD_ID_LEN];
    TEST_ASSERT_EQUAL_INT(CE_RESULT_ACCEPTED, command_engine_submit(&r, id));

    pending_command_t cmd;
    cmd_queue_pop("L1", &cmd);
    for (int i = 0; i < CHANNEL_COUNT; ++i) {
        TEST_ASSERT_EQUAL_UINT16(0, cmd.state.values[i]);
    }
}

static void test_preset_moonlight(void)
{
    setup();
    ce_request_t r;
    make_req(&r, CE_KIND_PRESET, "L1");
    r.preset = PRESET_MOONLIGHT;
    char id[CMD_ID_LEN];
    TEST_ASSERT_EQUAL_INT(CE_RESULT_ACCEPTED, command_engine_submit(&r, id));

    pending_command_t cmd;
    cmd_queue_pop("L1", &cmd);
    TEST_ASSERT_EQUAL_UINT16(1000, cmd.state.values[0]);
    TEST_ASSERT_EQUAL_UINT16(50,   cmd.state.values[8]);
}

static void test_preset_none_rejected(void)
{
    setup();
    ce_request_t r;
    make_req(&r, CE_KIND_PRESET, "L1");
    r.preset = PRESET_NONE;
    char id[CMD_ID_LEN];
    TEST_ASSERT_EQUAL_INT(CE_RESULT_INVALID_COMMAND, command_engine_submit(&r, id));
}

static void test_set_channels_replace(void)
{
    setup();
    ce_request_t r;
    make_req(&r, CE_KIND_SET_CHANNELS, "L1");
    r.replace = true;
    r.channels[0].name = "brightness"; r.channels[0].value = 800;
    r.channels[1].name = "blue";       r.channels[1].value = 40;
    r.channel_count = 2;
    char id[CMD_ID_LEN];
    TEST_ASSERT_EQUAL_INT(CE_RESULT_ACCEPTED, command_engine_submit(&r, id));

    pending_command_t cmd;
    cmd_queue_pop("L1", &cmd);
    TEST_ASSERT_EQUAL_UINT16(800, cmd.state.values[0]);
    TEST_ASSERT_EQUAL_UINT16(40,  cmd.state.values[2]);
    TEST_ASSERT_EQUAL_UINT16(0, cmd.state.values[1]);
    TEST_ASSERT_EQUAL_UINT16(0, cmd.state.values[8]);
}

static void test_set_channels_merge_uses_last_state(void)
{
    setup();
    channel_state_t base;
    for (int i = 0; i < CHANNEL_COUNT; ++i) base.values[i] = 500;
    light_registry_set_last_state("L1", &base);

    ce_request_t r;
    make_req(&r, CE_KIND_SET_CHANNELS, "L1");
    r.replace = false;
    r.channels[0].name = "brightness"; r.channels[0].value = 900;
    r.channel_count = 1;
    char id[CMD_ID_LEN];
    TEST_ASSERT_EQUAL_INT(CE_RESULT_ACCEPTED, command_engine_submit(&r, id));

    pending_command_t cmd;
    cmd_queue_pop("L1", &cmd);
    TEST_ASSERT_EQUAL_UINT16(900, cmd.state.values[0]);
    for (int i = 1; i < CHANNEL_COUNT; ++i) {
        TEST_ASSERT_EQUAL_UINT16(500, cmd.state.values[i]);
    }
}

static void test_set_channels_unknown_name(void)
{
    setup();
    ce_request_t r;
    make_req(&r, CE_KIND_SET_CHANNELS, "L1");
    r.replace = true;
    r.channels[0].name = "bogus"; r.channels[0].value = 100;
    r.channel_count = 1;
    char id[CMD_ID_LEN];
    TEST_ASSERT_EQUAL_INT(CE_RESULT_INVALID_CHANNEL, command_engine_submit(&r, id));
}

static void test_set_channels_out_of_range(void)
{
    setup();
    ce_request_t r;
    make_req(&r, CE_KIND_SET_CHANNELS, "L1");
    r.replace = true;
    r.channels[0].name = "brightness"; r.channels[0].value = 1001;
    r.channel_count = 1;
    char id[CMD_ID_LEN];
    TEST_ASSERT_EQUAL_INT(CE_RESULT_INVALID_INTENSITY, command_engine_submit(&r, id));
}

static void test_ramp_valid(void)
{
    setup();
    ce_request_t r;
    make_req(&r, CE_KIND_RAMP, "L1");
    r.ramp_from = 0;
    r.ramp_to   = 1000;
    r.ramp_duration_ms = 8000;
    r.ramp_steps = 20;
    char id[CMD_ID_LEN];
    TEST_ASSERT_EQUAL_INT(CE_RESULT_ACCEPTED, command_engine_submit(&r, id));

    pending_command_t cmd;
    cmd_queue_pop("L1", &cmd);
    TEST_ASSERT_EQUAL_INT(CMD_TYPE_RAMP, cmd.type);
    TEST_ASSERT_EQUAL_UINT16(0,    cmd.ramp_from);
    TEST_ASSERT_EQUAL_UINT16(1000, cmd.ramp_to);
    TEST_ASSERT_EQUAL_UINT16(20,   cmd.ramp_steps);
}

static void test_ramp_zero_steps_rejected(void)
{
    setup();
    ce_request_t r;
    make_req(&r, CE_KIND_RAMP, "L1");
    r.ramp_from = 0;
    r.ramp_to   = 1000;
    r.ramp_steps = 0;
    char id[CMD_ID_LEN];
    TEST_ASSERT_EQUAL_INT(CE_RESULT_INVALID_COMMAND, command_engine_submit(&r, id));
}

static void test_ramp_out_of_range_rejected(void)
{
    setup();
    ce_request_t r;
    make_req(&r, CE_KIND_RAMP, "L1");
    r.ramp_from = 0;
    r.ramp_to   = 5000;
    r.ramp_steps = 20;
    char id[CMD_ID_LEN];
    TEST_ASSERT_EQUAL_INT(CE_RESULT_INVALID_INTENSITY, command_engine_submit(&r, id));
}

static void test_caller_supplied_command_id_preserved(void)
{
    setup();
    ce_request_t r;
    make_req(&r, CE_KIND_POWER_ON, "L1");
    strncpy(r.command_id, "user-id-99", CMD_ID_LEN - 1);
    char id[CMD_ID_LEN];
    TEST_ASSERT_EQUAL_INT(CE_RESULT_ACCEPTED, command_engine_submit(&r, id));
    TEST_ASSERT_EQUAL_STRING("user-id-99", id);

    pending_command_t cmd;
    cmd_queue_pop("L1", &cmd);
    TEST_ASSERT_EQUAL_STRING("user-id-99", cmd.command_id);
}

static void test_generated_command_ids_are_unique(void)
{
    setup();
    ce_request_t r;
    make_req(&r, CE_KIND_RAMP, "L1");
    r.ramp_from = 0; r.ramp_to = 1000; r.ramp_steps = 5;
    char id1[CMD_ID_LEN], id2[CMD_ID_LEN];
    TEST_ASSERT_EQUAL_INT(CE_RESULT_ACCEPTED, command_engine_submit(&r, id1));
    TEST_ASSERT_EQUAL_INT(CE_RESULT_ACCEPTED, command_engine_submit(&r, id2));
    TEST_ASSERT_NOT_EQUAL_INT(0, strcmp(id1, id2));
}

static void test_queue_full(void)
{
    setup();
    ce_request_t r;
    make_req(&r, CE_KIND_RAMP, "L1");
    r.ramp_from = 0; r.ramp_to = 1000; r.ramp_steps = 5;
    for (int i = 0; i < CMD_QUEUE_DEPTH; ++i) {
        char id[CMD_ID_LEN];
        TEST_ASSERT_EQUAL_INT(CE_RESULT_ACCEPTED, command_engine_submit(&r, id));
    }
    char id[CMD_ID_LEN];
    TEST_ASSERT_EQUAL_INT(CE_RESULT_QUEUE_FULL, command_engine_submit(&r, id));
}

static void test_custom_timeouts_honored(void)
{
    setup();
    ce_request_t r;
    make_req(&r, CE_KIND_POWER_ON, "L1");
    r.scene_timeout_sec = 120;
    r.command_timeout_ms = 5000;
    char id[CMD_ID_LEN];
    TEST_ASSERT_EQUAL_INT(CE_RESULT_ACCEPTED, command_engine_submit(&r, id));

    pending_command_t cmd;
    cmd_queue_pop("L1", &cmd);
    TEST_ASSERT_EQUAL_UINT16(120, cmd.scene_timeout_sec);
    TEST_ASSERT_EQUAL_UINT32(5000, cmd.timeout_ms);
}

static void test_group_target_fans_out_to_members(void)
{
    setup();
    add_second_light_and_group();
    ce_request_t r;
    make_req(&r, CE_KIND_SET_CHANNELS, "G1");
    r.target_type = CE_TARGET_GROUP;
    r.replace = true;
    r.channels[0].name = "brightness"; r.channels[0].value = 700;
    r.channels[1].name = "blue";       r.channels[1].value = 500;
    r.channel_count = 2;
    char id[CMD_ID_LEN];
    TEST_ASSERT_EQUAL_INT(CE_RESULT_ACCEPTED, command_engine_submit(&r, id));
    TEST_ASSERT_EQUAL_size_t(1, cmd_queue_depth("L1"));
    TEST_ASSERT_EQUAL_size_t(1, cmd_queue_depth("L2"));

    pending_command_t cmd;
    cmd_queue_pop("L1", &cmd);
    TEST_ASSERT_EQUAL_UINT16(700, cmd.state.values[0]);
    TEST_ASSERT_EQUAL_UINT16(500, cmd.state.values[2]);
    cmd_queue_pop("L2", &cmd);
    TEST_ASSERT_EQUAL_UINT16(700, cmd.state.values[0]);
    TEST_ASSERT_EQUAL_UINT16(500, cmd.state.values[2]);
}

static void test_group_target_empty_or_missing_rejected(void)
{
    setup();
    ce_request_t r;
    make_req(&r, CE_KIND_POWER_ON, "NO_GROUP");
    r.target_type = CE_TARGET_GROUP;
    char id[CMD_ID_LEN];
    TEST_ASSERT_EQUAL_INT(CE_RESULT_INVALID_TARGET, command_engine_submit(&r, id));
}

void register_command_engine_tests(void)
{
    RUN_TEST(test_unknown_target);
    RUN_TEST(test_empty_target);
    RUN_TEST(test_power_on_enqueues_preset_on);
    RUN_TEST(test_power_off_enqueues_preset_off);
    RUN_TEST(test_preset_moonlight);
    RUN_TEST(test_preset_none_rejected);
    RUN_TEST(test_set_channels_replace);
    RUN_TEST(test_set_channels_merge_uses_last_state);
    RUN_TEST(test_set_channels_unknown_name);
    RUN_TEST(test_set_channels_out_of_range);
    RUN_TEST(test_ramp_valid);
    RUN_TEST(test_ramp_zero_steps_rejected);
    RUN_TEST(test_ramp_out_of_range_rejected);
    RUN_TEST(test_caller_supplied_command_id_preserved);
    RUN_TEST(test_generated_command_ids_are_unique);
    RUN_TEST(test_queue_full);
    RUN_TEST(test_custom_timeouts_honored);
    RUN_TEST(test_group_target_fans_out_to_members);
    RUN_TEST(test_group_target_empty_or_missing_rejected);
}
