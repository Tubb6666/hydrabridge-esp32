/* Phase 0.4b: host tests pinning config defaults to the spec values
 * in docs/esp32-hydra64hd-controller-plan.md "Persistent Data Model".
 * If any of these fail, either the defaults regressed or the spec
 * changed -- update both together.
 */
#include "unity.h"
#include "all_tests.h"
#include "config_store.h"

#include <string.h>

/* ---- controller defaults (refs spec L199-L210) ---- */

static void test_controller_defaults_match_spec(void)
{
    config_controller_t c;
    config_defaults_controller(&c);
    TEST_ASSERT_EQUAL_STRING("hydra-ctrl-01",    c.controller_id);
    TEST_ASSERT_EQUAL_STRING("Hydra Controller", c.device_name);
    TEST_ASSERT_EQUAL_STRING("local",            c.timezone);
    TEST_ASSERT_EQUAL_STRING("0.1.0",            c.firmware_version);
    TEST_ASSERT_TRUE(c.ota_enabled);
    TEST_ASSERT_EQUAL_UINT8(4,      c.max_registered_lights);
    TEST_ASSERT_EQUAL_UINT32(30000, c.ble_idle_disconnect_ms);
    TEST_ASSERT_EQUAL_UINT8(1,      c.ble_command_concurrency);
}

/* ---- modbus defaults (refs spec L213-L231 + L234-L240) ---- */

static void test_modbus_defaults_match_spec(void)
{
    config_modbus_t m;
    config_defaults_modbus(&m);
    TEST_ASSERT_FALSE(m.enabled);
    TEST_ASSERT_FALSE(m.master_mode_enabled);
    TEST_ASSERT_EQUAL_UINT8(10,    m.slave_address);
    TEST_ASSERT_EQUAL_UINT32(19200, m.baud_rate);
    TEST_ASSERT_EQUAL_UINT8(8,     m.data_bits);
    TEST_ASSERT_EQUAL_INT(MODBUS_PARITY_EVEN, m.parity);
    TEST_ASSERT_EQUAL_UINT8(1,     m.stop_bits);
    TEST_ASSERT_EQUAL_UINT8(1,     m.uart_port);
    /* RS485 pinout for this controller build. */
    TEST_ASSERT_EQUAL_INT8(17,     m.tx_pin);
    TEST_ASSERT_EQUAL_INT8(18,     m.rx_pin);
    TEST_ASSERT_EQUAL_INT8(4,      m.rts_de_pin);
    TEST_ASSERT_EQUAL_UINT32(250,   m.response_timeout_ms);
    TEST_ASSERT_EQUAL_UINT32(30000, m.command_watchdog_ms);
}

/* ---- wifi defaults (refs spec L252-L258) ---- */

static void test_wifi_defaults_match_spec(void)
{
    config_wifi_t w;
    config_defaults_wifi(&w);
    TEST_ASSERT_TRUE(w.enabled);
    TEST_ASSERT_EQUAL_STRING("", w.ssid);
    TEST_ASSERT_EQUAL_STRING("", w.password);
    TEST_ASSERT_TRUE(w.ap_fallback_enabled);
    TEST_ASSERT_EQUAL_STRING("HydraBridge-Setup", w.ap_ssid);
    TEST_ASSERT_EQUAL_STRING("hydrabridge", w.ap_password);
}

/* ---- mqtt defaults (refs spec L263-L273) ---- */

static void test_mqtt_defaults_match_spec(void)
{
    config_mqtt_t m;
    config_defaults_mqtt(&m);
    TEST_ASSERT_FALSE(m.enabled);
    TEST_ASSERT_EQUAL_STRING("", m.host);
    TEST_ASSERT_EQUAL_UINT16(1883, m.port);
    TEST_ASSERT_FALSE(m.use_tls);
    TEST_ASSERT_EQUAL_STRING("", m.username);
    TEST_ASSERT_EQUAL_STRING("", m.password);
    TEST_ASSERT_EQUAL_STRING("hydrabridge-esp32", m.client_id);
    TEST_ASSERT_EQUAL_UINT16(60, m.keepalive_sec);
    TEST_ASSERT_EQUAL_STRING("aihydra",       m.base_topic);
    TEST_ASSERT_FALSE(m.home_assistant_discovery);
    TEST_ASSERT_EQUAL_STRING("homeassistant", m.home_assistant_prefix);
}

/* ---- defaults overwrite prior garbage (regression test) ---- */

static void test_defaults_overwrite_garbage(void)
{
    config_modbus_t m;
    memset(&m, 0xFF, sizeof m);
    config_defaults_modbus(&m);
    TEST_ASSERT_EQUAL_UINT8(10, m.slave_address);
    TEST_ASSERT_EQUAL_UINT32(19200, m.baud_rate);
}

/* ---- null-safe ---- */

static void test_defaults_null_safe(void)
{
    config_defaults_controller(NULL);
    config_defaults_modbus(NULL);
    config_defaults_wifi(NULL);
    config_defaults_mqtt(NULL);
    TEST_PASS();
}

/* ---- string-size budgets are sane ---- */

static void test_size_budgets(void)
{
    /* WiFi PSK must accommodate WPA2's 63-char max + NUL. */
    TEST_ASSERT_TRUE(CONFIG_WIFI_PSK_LEN >= 64);
    /* SSID must accommodate 802.11's 32-char max + NUL. */
    TEST_ASSERT_TRUE(CONFIG_WIFI_SSID_LEN >= 33);
}

/* ---- registration ---- */

void register_config_store_tests(void)
{
    RUN_TEST(test_controller_defaults_match_spec);
    RUN_TEST(test_modbus_defaults_match_spec);
    RUN_TEST(test_wifi_defaults_match_spec);
    RUN_TEST(test_mqtt_defaults_match_spec);
    RUN_TEST(test_defaults_overwrite_garbage);
    RUN_TEST(test_defaults_null_safe);
    RUN_TEST(test_size_budgets);
}
