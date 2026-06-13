/* Phase 0.2 smoke test -- proves the host-test harness compiles, links,
 * and runs at least one Unity assertion. Real protocol tests start at
 * Phase 1.1 (CRC16). */
#include "unity.h"
#include "all_tests.h"

static void test_harness_links_and_runs(void)
{
    TEST_ASSERT_EQUAL_INT(2, 1 + 1);
}

void register_smoke_tests(void)
{
    RUN_TEST(test_harness_links_and_runs);
}
