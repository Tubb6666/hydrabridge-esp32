/* Phase 3.2b: command_queue host tests for the coalescing rule and
 * ring-buffer behavior. Uses a fake clock so expiry tests are
 * deterministic. */
#include "unity.h"
#include "all_tests.h"
#include "command_queue.h"

#include <string.h>
#include <stdio.h>

static uint64_t s_fake_now = 0;
static uint64_t fake_clock(void) { return s_fake_now; }

static void install_fake_clock(uint64_t t)
{
    s_fake_now = t;
    cmd_queue_set_clock_fn(fake_clock);
}

static void make_cmd(pending_command_t *out, const char *id, const char *light_id,
                     cmd_type_t type, uint32_t timeout_ms)
{
    memset(out, 0, sizeof *out);
    strncpy(out->command_id, id, CMD_ID_LEN - 1);
    strncpy(out->light_id,  light_id, LIGHT_ID_LEN - 1);
    out->type        = type;
    out->source      = CMD_SOURCE_MQTT;
    out->timeout_ms  = timeout_ms;
    out->scene_timeout_sec = 60;
}

static void test_starts_empty(void)
{
    cmd_queue_reset();
    TEST_ASSERT_EQUAL_size_t(0, cmd_queue_depth("anything"));
    pending_command_t out;
    TEST_ASSERT_EQUAL_INT(-1, cmd_queue_pop("anything", &out));
    TEST_ASSERT_EQUAL_INT(-1, cmd_queue_peek("anything", &out));
}

static void test_push_pop_fifo(void)
{
    cmd_queue_reset();
    install_fake_clock(100);

    pending_command_t a, b, c;
    make_cmd(&a, "id-a", "L1", CMD_TYPE_RAMP, 0);
    make_cmd(&b, "id-b", "L1", CMD_TYPE_IDENTIFY, 0);
    make_cmd(&c, "id-c", "L1", CMD_TYPE_RAMP, 0);

    TEST_ASSERT_EQUAL_INT(0, cmd_queue_push(&a));
    TEST_ASSERT_EQUAL_INT(0, cmd_queue_push(&b));
    TEST_ASSERT_EQUAL_INT(0, cmd_queue_push(&c));
    TEST_ASSERT_EQUAL_size_t(3, cmd_queue_depth("L1"));

    pending_command_t got;
    TEST_ASSERT_EQUAL_INT(0, cmd_queue_pop("L1", &got)); TEST_ASSERT_EQUAL_STRING("id-a", got.command_id);
    TEST_ASSERT_EQUAL_INT(0, cmd_queue_pop("L1", &got)); TEST_ASSERT_EQUAL_STRING("id-b", got.command_id);
    TEST_ASSERT_EQUAL_INT(0, cmd_queue_pop("L1", &got)); TEST_ASSERT_EQUAL_STRING("id-c", got.command_id);
    TEST_ASSERT_EQUAL_INT(-1, cmd_queue_pop("L1", &got));
}

static void test_coalesce_set_channels(void)
{
    cmd_queue_reset();
    install_fake_clock(100);

    pending_command_t a;
    make_cmd(&a, "set-1", "L1", CMD_TYPE_SET_CHANNELS, 0);
    a.state.values[0] = 100;
    TEST_ASSERT_EQUAL_INT(0, cmd_queue_push(&a));

    pending_command_t b;
    make_cmd(&b, "set-2", "L1", CMD_TYPE_SET_CHANNELS, 0);
    b.state.values[0] = 500;
    s_fake_now = 200;
    TEST_ASSERT_EQUAL_INT(0, cmd_queue_push(&b));
    TEST_ASSERT_EQUAL_size_t(1, cmd_queue_depth("L1"));

    pending_command_t got;
    TEST_ASSERT_EQUAL_INT(0, cmd_queue_pop("L1", &got));
    TEST_ASSERT_EQUAL_STRING("set-2", got.command_id);
    TEST_ASSERT_EQUAL_UINT16(500, got.state.values[0]);
    TEST_ASSERT_EQUAL_UINT64(200, got.enqueue_ms);
}

static void test_coalesce_only_set_channels(void)
{
    cmd_queue_reset();
    install_fake_clock(100);

    pending_command_t r1, r2;
    make_cmd(&r1, "ramp-1", "L1", CMD_TYPE_RAMP, 0);
    make_cmd(&r2, "ramp-2", "L1", CMD_TYPE_RAMP, 0);
    cmd_queue_push(&r1);
    cmd_queue_push(&r2);
    TEST_ASSERT_EQUAL_size_t(2, cmd_queue_depth("L1"));

    pending_command_t id1, id2;
    make_cmd(&id1, "id-1", "L2", CMD_TYPE_IDENTIFY, 0);
    make_cmd(&id2, "id-2", "L2", CMD_TYPE_IDENTIFY, 0);
    cmd_queue_push(&id1);
    cmd_queue_push(&id2);
    TEST_ASSERT_EQUAL_size_t(2, cmd_queue_depth("L2"));
}

static void test_set_channels_after_ramp_does_not_coalesce_with_ramp(void)
{
    cmd_queue_reset();
    pending_command_t ramp, set;
    make_cmd(&ramp, "ramp", "L1", CMD_TYPE_RAMP, 0);
    make_cmd(&set,  "set",  "L1", CMD_TYPE_SET_CHANNELS, 0);
    cmd_queue_push(&ramp);
    cmd_queue_push(&set);
    TEST_ASSERT_EQUAL_size_t(2, cmd_queue_depth("L1"));

    pending_command_t got;
    cmd_queue_pop("L1", &got); TEST_ASSERT_EQUAL_STRING("ramp", got.command_id);
    cmd_queue_pop("L1", &got); TEST_ASSERT_EQUAL_STRING("set",  got.command_id);
}

static void test_capacity_full_returns_error(void)
{
    cmd_queue_reset();
    pending_command_t cmd;
    char id[16];
    for (int i = 0; i < CMD_QUEUE_DEPTH; ++i) {
        snprintf(id, sizeof id, "r-%d", i);
        make_cmd(&cmd, id, "L1", CMD_TYPE_RAMP, 0);
        TEST_ASSERT_EQUAL_INT(0, cmd_queue_push(&cmd));
    }
    make_cmd(&cmd, "overflow", "L1", CMD_TYPE_RAMP, 0);
    TEST_ASSERT_EQUAL_INT(-1, cmd_queue_push(&cmd));
}

static void test_ring_wrap(void)
{
    cmd_queue_reset();
    pending_command_t cmd, got;
    char id[16];

    for (int round = 0; round < 3; ++round) {
        for (int i = 0; i < CMD_QUEUE_DEPTH; ++i) {
            snprintf(id, sizeof id, "r%d-%d", round, i);
            make_cmd(&cmd, id, "L1", CMD_TYPE_RAMP, 0);
            TEST_ASSERT_EQUAL_INT(0, cmd_queue_push(&cmd));
        }
        TEST_ASSERT_EQUAL_size_t(CMD_QUEUE_DEPTH, cmd_queue_depth("L1"));

        for (int i = 0; i < CMD_QUEUE_DEPTH; ++i) {
            TEST_ASSERT_EQUAL_INT(0, cmd_queue_pop("L1", &got));
            snprintf(id, sizeof id, "r%d-%d", round, i);
            TEST_ASSERT_EQUAL_STRING(id, got.command_id);
        }
        TEST_ASSERT_EQUAL_size_t(0, cmd_queue_depth("L1"));
    }
}

static void test_independent_queues(void)
{
    cmd_queue_reset();
    pending_command_t a, b;
    make_cmd(&a, "a", "L1", CMD_TYPE_RAMP, 0);
    make_cmd(&b, "b", "L2", CMD_TYPE_RAMP, 0);
    cmd_queue_push(&a);
    cmd_queue_push(&b);
    TEST_ASSERT_EQUAL_size_t(1, cmd_queue_depth("L1"));
    TEST_ASSERT_EQUAL_size_t(1, cmd_queue_depth("L2"));

    pending_command_t got;
    cmd_queue_pop("L1", &got);
    TEST_ASSERT_EQUAL_STRING("a", got.command_id);
    TEST_ASSERT_EQUAL_size_t(0, cmd_queue_depth("L1"));
    TEST_ASSERT_EQUAL_size_t(1, cmd_queue_depth("L2"));
}

static void test_slot_reused_after_drain(void)
{
    cmd_queue_reset();
    pending_command_t cmd;
    for (int i = 0; i < LIGHT_REGISTRY_CAPACITY; ++i) {
        char light_id[16];
        snprintf(light_id, sizeof light_id, "L%d", i);
        make_cmd(&cmd, "x", light_id, CMD_TYPE_RAMP, 0);
        TEST_ASSERT_EQUAL_INT(0, cmd_queue_push(&cmd));
    }
    make_cmd(&cmd, "x", "L99", CMD_TYPE_RAMP, 0);
    TEST_ASSERT_EQUAL_INT(-1, cmd_queue_push(&cmd));

    pending_command_t got;
    cmd_queue_pop("L0", &got);

    make_cmd(&cmd, "x", "L99", CMD_TYPE_RAMP, 0);
    TEST_ASSERT_EQUAL_INT(0, cmd_queue_push(&cmd));
}

static void test_expire_drops_old(void)
{
    cmd_queue_reset();
    install_fake_clock(1000);

    pending_command_t a, b;
    make_cmd(&a, "a", "L1", CMD_TYPE_RAMP, 500);
    make_cmd(&b, "b", "L1", CMD_TYPE_RAMP, 5000);
    cmd_queue_push(&a);
    cmd_queue_push(&b);

    s_fake_now = 2000;
    TEST_ASSERT_EQUAL_size_t(1, cmd_queue_expire());
    TEST_ASSERT_EQUAL_size_t(1, cmd_queue_depth("L1"));

    pending_command_t got;
    cmd_queue_pop("L1", &got);
    TEST_ASSERT_EQUAL_STRING("b", got.command_id);
}

static void test_expire_zero_timeout_never_expires(void)
{
    cmd_queue_reset();
    install_fake_clock(1000);
    pending_command_t a;
    make_cmd(&a, "a", "L1", CMD_TYPE_RAMP, 0);
    cmd_queue_push(&a);

    s_fake_now = (uint64_t)1e15;
    TEST_ASSERT_EQUAL_size_t(0, cmd_queue_expire());
    TEST_ASSERT_EQUAL_size_t(1, cmd_queue_depth("L1"));
}

static void test_push_null_or_empty_id_fails(void)
{
    cmd_queue_reset();
    pending_command_t cmd;
    memset(&cmd, 0, sizeof cmd);
    cmd.type = CMD_TYPE_RAMP;
    TEST_ASSERT_EQUAL_INT(-1, cmd_queue_push(&cmd));
    TEST_ASSERT_EQUAL_INT(-1, cmd_queue_push(NULL));
}

void register_command_queue_tests(void)
{
    RUN_TEST(test_starts_empty);
    RUN_TEST(test_push_pop_fifo);
    RUN_TEST(test_coalesce_set_channels);
    RUN_TEST(test_coalesce_only_set_channels);
    RUN_TEST(test_set_channels_after_ramp_does_not_coalesce_with_ramp);
    RUN_TEST(test_capacity_full_returns_error);
    RUN_TEST(test_ring_wrap);
    RUN_TEST(test_independent_queues);
    RUN_TEST(test_slot_reused_after_drain);
    RUN_TEST(test_expire_drops_old);
    RUN_TEST(test_expire_zero_timeout_never_expires);
    RUN_TEST(test_push_null_or_empty_id_fails);
}
