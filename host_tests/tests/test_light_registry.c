/* Phase 3.1b: light_registry host tests for in-memory CRUD and lookup
 * behavior. NVS persistence is ESP-IDF-only and tested on hardware. */
#include "unity.h"
#include "all_tests.h"
#include "light_registry.h"

#include <string.h>
#include <stdio.h>

static void make_light(registered_light_t *out, const char *id, const char *serial,
                       uint8_t a0, uint8_t a1, uint8_t a2, uint8_t a3, uint8_t a4, uint8_t a5)
{
    memset(out, 0, sizeof *out);
    strncpy(out->light_id, id, LIGHT_ID_LEN - 1);
    strncpy(out->display_name, "test light", LIGHT_NAME_LEN - 1);
    strncpy(out->serial, serial, LIGHT_SERIAL_LEN - 1);
    out->ble_addr[0] = a0; out->ble_addr[1] = a1; out->ble_addr[2] = a2;
    out->ble_addr[3] = a3; out->ble_addr[4] = a4; out->ble_addr[5] = a5;
    out->ble_addr_type = BLE_ADDR_PUBLIC;
    out->model = 335;
    out->enabled = true;
    out->last_seen_rssi = -55;
}

static void test_starts_empty(void)
{
    light_registry_reset();
    TEST_ASSERT_EQUAL_size_t(0, light_registry_count());
    TEST_ASSERT_EQUAL_size_t(0, group_registry_count());
    TEST_ASSERT_NULL(light_registry_at(0));
    TEST_ASSERT_NULL(light_registry_get("anything"));
}

static void test_add_one_and_get(void)
{
    light_registry_reset();
    registered_light_t l;
    make_light(&l, "hydra-1", "SER1", 0x1C, 0xBC, 0xEC, 0x0B, 0xE6, 0xD2);
    TEST_ASSERT_EQUAL_INT(0, light_registry_add(&l));
    TEST_ASSERT_EQUAL_size_t(1, light_registry_count());

    const registered_light_t *got = light_registry_get("hydra-1");
    TEST_ASSERT_NOT_NULL(got);
    TEST_ASSERT_EQUAL_STRING("hydra-1", got->light_id);
    TEST_ASSERT_EQUAL_STRING("SER1",    got->serial);
    TEST_ASSERT_EQUAL_UINT16(335, got->model);
    TEST_ASSERT_TRUE(got->enabled);
}

static void test_add_duplicate_id_fails(void)
{
    light_registry_reset();
    registered_light_t a, b;
    make_light(&a, "dup", "S1", 1,2,3,4,5,6);
    make_light(&b, "dup", "S2", 7,8,9,10,11,12);
    TEST_ASSERT_EQUAL_INT(0,  light_registry_add(&a));
    TEST_ASSERT_EQUAL_INT(-1, light_registry_add(&b));
    TEST_ASSERT_EQUAL_size_t(1, light_registry_count());
    TEST_ASSERT_EQUAL_STRING("S1", light_registry_get("dup")->serial);
}

static void test_capacity_limit(void)
{
    light_registry_reset();
    registered_light_t l;
    char ids[16];
    for (int i = 0; i < LIGHT_REGISTRY_CAPACITY; ++i) {
        snprintf(ids, sizeof ids, "l-%d", i);
        make_light(&l, ids, ids, (uint8_t)i, 0, 0, 0, 0, 0);
        TEST_ASSERT_EQUAL_INT(0, light_registry_add(&l));
    }
    snprintf(ids, sizeof ids, "l-%d", LIGHT_REGISTRY_CAPACITY);
    make_light(&l, ids, ids, 0xff, 0, 0, 0, 0, 0);
    TEST_ASSERT_EQUAL_INT(-1, light_registry_add(&l));
    TEST_ASSERT_EQUAL_size_t(LIGHT_REGISTRY_CAPACITY, light_registry_count());
}

static void test_remove_compacts_table(void)
{
    light_registry_reset();
    registered_light_t l;
    make_light(&l, "a", "SA", 1,0,0,0,0,0); TEST_ASSERT_EQUAL_INT(0, light_registry_add(&l));
    make_light(&l, "b", "SB", 2,0,0,0,0,0); TEST_ASSERT_EQUAL_INT(0, light_registry_add(&l));
    make_light(&l, "c", "SC", 3,0,0,0,0,0); TEST_ASSERT_EQUAL_INT(0, light_registry_add(&l));

    TEST_ASSERT_EQUAL_INT(0, light_registry_remove("b"));
    TEST_ASSERT_EQUAL_size_t(2, light_registry_count());
    TEST_ASSERT_NOT_NULL(light_registry_get("a"));
    TEST_ASSERT_NULL(light_registry_get("b"));
    TEST_ASSERT_NOT_NULL(light_registry_get("c"));
}

static void test_remove_not_found(void)
{
    light_registry_reset();
    TEST_ASSERT_EQUAL_INT(-1, light_registry_remove("nope"));
}

static void test_get_by_serial(void)
{
    light_registry_reset();
    registered_light_t l;
    make_light(&l, "hydra-9Q9B", "9Q9B0BE6D2R1EB", 0x1C, 0xBC, 0xEC, 0x0B, 0xE6, 0xD2);
    TEST_ASSERT_EQUAL_INT(0, light_registry_add(&l));

    const registered_light_t *got = light_registry_get_by_serial("9Q9B0BE6D2R1EB");
    TEST_ASSERT_NOT_NULL(got);
    TEST_ASSERT_EQUAL_STRING("hydra-9Q9B", got->light_id);

    TEST_ASSERT_NULL(light_registry_get_by_serial("OTHER"));
    TEST_ASSERT_NULL(light_registry_get_by_serial(NULL));
}

static void test_get_by_addr(void)
{
    light_registry_reset();
    registered_light_t l;
    uint8_t addr[BLE_ADDR_BYTES] = {0x1C, 0xBC, 0xEC, 0x0B, 0xE6, 0xD2};
    make_light(&l, "addr-test", "S", addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
    TEST_ASSERT_EQUAL_INT(0, light_registry_add(&l));

    const registered_light_t *got = light_registry_get_by_addr(addr);
    TEST_ASSERT_NOT_NULL(got);
    TEST_ASSERT_EQUAL_STRING("addr-test", got->light_id);

    uint8_t other[BLE_ADDR_BYTES] = {0, 0, 0, 0, 0, 0};
    TEST_ASSERT_NULL(light_registry_get_by_addr(other));
    TEST_ASSERT_NULL(light_registry_get_by_addr(NULL));
}

static void test_iterate(void)
{
    light_registry_reset();
    registered_light_t l;
    make_light(&l, "i0", "S0", 0,0,0,0,0,0); light_registry_add(&l);
    make_light(&l, "i1", "S1", 1,0,0,0,0,0); light_registry_add(&l);

    TEST_ASSERT_EQUAL_STRING("i0", light_registry_at(0)->light_id);
    TEST_ASSERT_EQUAL_STRING("i1", light_registry_at(1)->light_id);
    TEST_ASSERT_NULL(light_registry_at(2));
    TEST_ASSERT_NULL(light_registry_at(99));
}

static void test_mutators(void)
{
    light_registry_reset();
    registered_light_t l;
    make_light(&l, "m", "S", 1,2,3,4,5,6);
    light_registry_add(&l);

    channel_state_t s;
    for (int i = 0; i < CHANNEL_COUNT; ++i) s.values[i] = (uint16_t)(i * 100);
    TEST_ASSERT_EQUAL_INT(0,  light_registry_set_last_state("m", &s));
    TEST_ASSERT_EQUAL_INT(-1, light_registry_set_last_state("nope", &s));

    TEST_ASSERT_EQUAL_INT(0,  light_registry_set_rssi("m", -77));
    TEST_ASSERT_EQUAL_INT(-1, light_registry_set_rssi("nope", -77));

    TEST_ASSERT_EQUAL_INT(0,  light_registry_set_enabled("m", false));

    const registered_light_t *got = light_registry_get("m");
    TEST_ASSERT_EQUAL_UINT16(200, got->last_state.values[2]);
    TEST_ASSERT_EQUAL_INT8(-77, got->last_seen_rssi);
    TEST_ASSERT_FALSE(got->enabled);
}

/* ---- groups ---- */

static void make_group(light_group_t *out, const char *id, const char *l1, const char *l2)
{
    memset(out, 0, sizeof *out);
    strncpy(out->group_id, id, GROUP_ID_LEN - 1);
    strncpy(out->display_name, "test group", LIGHT_NAME_LEN - 1);
    out->member_count = 0;
    if (l1) { strncpy(out->light_ids[out->member_count++], l1, LIGHT_ID_LEN - 1); }
    if (l2) { strncpy(out->light_ids[out->member_count++], l2, LIGHT_ID_LEN - 1); }
    out->enabled = true;
}

static void test_group_add_remove(void)
{
    light_registry_reset();
    light_group_t g;
    make_group(&g, "g1", "a", "b");
    TEST_ASSERT_EQUAL_INT(0, group_registry_add(&g));
    TEST_ASSERT_EQUAL_size_t(1, group_registry_count());
    TEST_ASSERT_NOT_NULL(group_registry_get("g1"));
    TEST_ASSERT_EQUAL_UINT8(2, group_registry_get("g1")->member_count);

    TEST_ASSERT_EQUAL_INT(0, group_registry_remove("g1"));
    TEST_ASSERT_EQUAL_size_t(0, group_registry_count());
    TEST_ASSERT_NULL(group_registry_get("g1"));
}

static void test_group_dup_fails(void)
{
    light_registry_reset();
    light_group_t g;
    make_group(&g, "g", "a", NULL);
    TEST_ASSERT_EQUAL_INT(0,  group_registry_add(&g));
    TEST_ASSERT_EQUAL_INT(-1, group_registry_add(&g));
}

static void test_removing_light_drops_it_from_groups(void)
{
    light_registry_reset();
    registered_light_t l;
    make_light(&l, "a", "SA", 1,0,0,0,0,0); light_registry_add(&l);
    make_light(&l, "b", "SB", 2,0,0,0,0,0); light_registry_add(&l);

    light_group_t g;
    make_group(&g, "g", "a", "b");
    TEST_ASSERT_EQUAL_INT(0, group_registry_add(&g));

    TEST_ASSERT_EQUAL_INT(0, light_registry_remove("a"));
    const light_group_t *got = group_registry_get("g");
    TEST_ASSERT_NOT_NULL(got);
    TEST_ASSERT_EQUAL_UINT8(1, got->member_count);
    TEST_ASSERT_EQUAL_STRING("b", got->light_ids[0]);
}

void register_light_registry_tests(void)
{
    RUN_TEST(test_starts_empty);
    RUN_TEST(test_add_one_and_get);
    RUN_TEST(test_add_duplicate_id_fails);
    RUN_TEST(test_capacity_limit);
    RUN_TEST(test_remove_compacts_table);
    RUN_TEST(test_remove_not_found);
    RUN_TEST(test_get_by_serial);
    RUN_TEST(test_get_by_addr);
    RUN_TEST(test_iterate);
    RUN_TEST(test_mutators);
    RUN_TEST(test_group_add_remove);
    RUN_TEST(test_group_dup_fails);
    RUN_TEST(test_removing_light_drops_it_from_groups);
}
