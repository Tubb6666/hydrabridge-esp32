/* Phase 0.3b: host tests for the event_log ring + redaction. */
#include "unity.h"
#include "all_tests.h"
#include "event_log.h"

#include <string.h>
#include <stdio.h>

/* ---- helpers ---- */

static uint64_t s_fake_now = 0;
static uint64_t fake_clock(void) { return s_fake_now; }

static void emit_simple(const char *code, const char *msg)
{
    event_log_emit(EVENT_LEVEL_INFO, code, NULL, NULL, msg);
}

/* ---- ring tests ---- */

static void test_emit_then_get(void)
{
    event_log_reset();
    emit_simple("boot", "hello");
    emit_simple("ble", "ready");
    TEST_ASSERT_EQUAL_UINT(2, event_log_count());

    event_log_entry_t e;
    TEST_ASSERT_TRUE(event_log_get(0, &e));
    TEST_ASSERT_EQUAL_STRING("boot", e.code);
    TEST_ASSERT_EQUAL_STRING("hello", e.message);
    TEST_ASSERT_EQUAL_INT(EVENT_LEVEL_INFO, e.level);

    TEST_ASSERT_TRUE(event_log_get(1, &e));
    TEST_ASSERT_EQUAL_STRING("ble", e.code);
    TEST_ASSERT_EQUAL_STRING("ready", e.message);
}

static void test_ring_wraps_at_capacity(void)
{
    event_log_reset();
    char msg[16];
    for (int i = 0; i < EVENT_LOG_CAPACITY + 5; ++i) {
        snprintf(msg, sizeof msg, "evt%d", i);
        emit_simple("test", msg);
    }
    TEST_ASSERT_EQUAL_UINT(EVENT_LOG_CAPACITY, event_log_count());

    /* Oldest entry should now be the one whose body is "evt5"
     * (entries 0..4 got overwritten). */
    event_log_entry_t e;
    TEST_ASSERT_TRUE(event_log_get(0, &e));
    TEST_ASSERT_EQUAL_STRING("evt5", e.message);

    /* Newest entry should be the last one emitted. */
    TEST_ASSERT_TRUE(event_log_get(EVENT_LOG_CAPACITY - 1, &e));
    char expected[16];
    snprintf(expected, sizeof expected, "evt%d", EVENT_LOG_CAPACITY + 5 - 1);
    TEST_ASSERT_EQUAL_STRING(expected, e.message);
}

static void test_get_out_of_range(void)
{
    event_log_reset();
    emit_simple("x", "y");
    event_log_entry_t e;
    TEST_ASSERT_FALSE(event_log_get(1, &e));
    TEST_ASSERT_FALSE(event_log_get(999, &e));
}

static void test_reset_clears(void)
{
    event_log_reset();
    emit_simple("a", "b");
    emit_simple("c", "d");
    event_log_reset();
    TEST_ASSERT_EQUAL_UINT(0, event_log_count());
}

static void test_clock_fn_override(void)
{
    event_log_reset();
    s_fake_now = 4242;
    event_log_set_clock_fn(fake_clock);
    emit_simple("tick", "now");
    event_log_entry_t e;
    TEST_ASSERT_TRUE(event_log_get(0, &e));
    TEST_ASSERT_EQUAL_UINT64(4242, e.uptime_ms);
    event_log_set_clock_fn(NULL); /* restore default */
}

static void test_truncates_long_message(void)
{
    event_log_reset();
    char big[EVENT_LOG_MSG_MAX * 2];
    memset(big, 'A', sizeof big - 1);
    big[sizeof big - 1] = '\0';
    emit_simple("big", big);
    event_log_entry_t e;
    TEST_ASSERT_TRUE(event_log_get(0, &e));
    TEST_ASSERT_EQUAL_UINT(EVENT_LOG_MSG_MAX - 1, strlen(e.message));
    TEST_ASSERT_EQUAL_CHAR('\0', e.message[EVENT_LOG_MSG_MAX - 1]);
}

/* ---- redaction tests ---- */

static void test_redact_kv_equals(void)
{
    char buf[64];
    strcpy(buf, "wifi password=hunter2 ok");
    event_log_redact(buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("wifi password=*** ok", buf);
}

static void test_redact_kv_colon(void)
{
    char buf[64];
    strcpy(buf, "mqtt PASSWORD: s3cret continuing");
    event_log_redact(buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("mqtt PASSWORD: *** continuing", buf);
}

static void test_redact_json_quoted(void)
{
    char buf[80];
    strcpy(buf, "{\"password\":\"hunter2\",\"host\":\"x\"}");
    event_log_redact(buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("{\"password\":\"***\",\"host\":\"x\"}", buf);
}

static void test_redact_skips_no_separator(void)
{
    char buf[64];
    strcpy(buf, "the password feels good today");
    event_log_redact(buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("the password feels good today", buf);
}

static void test_redact_multiple_hits(void)
{
    char buf[80];
    strcpy(buf, "password=aaa secret=bbb");
    event_log_redact(buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("password=*** secret=***", buf);
}

static void test_emit_applies_redaction(void)
{
    event_log_reset();
    event_log_emit(EVENT_LEVEL_WARN, "wifi_conf", NULL, NULL,
                   "connecting password=topsecret host=192.168.1.1");
    event_log_entry_t e;
    TEST_ASSERT_TRUE(event_log_get(0, &e));
    TEST_ASSERT_NULL(strstr(e.message, "topsecret"));
    TEST_ASSERT_NOT_NULL(strstr(e.message, "password=***"));
}

/* ---- registration ---- */

void register_event_log_tests(void)
{
    RUN_TEST(test_emit_then_get);
    RUN_TEST(test_ring_wraps_at_capacity);
    RUN_TEST(test_get_out_of_range);
    RUN_TEST(test_reset_clears);
    RUN_TEST(test_clock_fn_override);
    RUN_TEST(test_truncates_long_message);
    RUN_TEST(test_redact_kv_equals);
    RUN_TEST(test_redact_kv_colon);
    RUN_TEST(test_redact_json_quoted);
    RUN_TEST(test_redact_skips_no_separator);
    RUN_TEST(test_redact_multiple_hits);
    RUN_TEST(test_emit_applies_redaction);
}
