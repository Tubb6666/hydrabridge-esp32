/*
 * Host-test runner. Each module's tests/test_<module>.c exposes
 *   void register_<module>_tests(void)
 * which internally calls RUN_TEST(test_fn) for each Unity case.
 * Add a new module by declaring its register fn in all_tests.h and
 * invoking it inside main(). Keep this file boring -- all test logic
 * lives in tests/.
 */
#include "unity.h"
#include "all_tests.h"

void setUp(void) {}
void tearDown(void) {}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    UNITY_BEGIN();
    register_smoke_tests();
    register_event_log_tests();
    register_config_store_tests();
    register_crc16_tests();
    register_fsci_builder_tests();
    register_fsci_parser_tests();
    register_channel_model_tests();
    register_hydra64hd_tests();
    register_preset_engine_tests();
    /* Future phases append calls here:
     * register_crc16_tests();           (1.1)
     * register_fsci_builder_tests();    (1.2)
     * register_fsci_parser_tests();     (1.3)
     * register_channel_model_tests();   (1.4)
     * register_hydra64hd_tests();       (1.5)
     * register_preset_engine_tests();   (1.6)
     */
    return UNITY_END();
}
