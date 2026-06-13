#include "ble_scanner.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "esp_err.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
static const char *TAG = "ble_scanner";
#endif

bool ble_scanner_is_ai_model(uint16_t model)
{
    return (model >= 320 && model <= 342);
}

int ble_scanner_parse_manuf(const uint8_t *data, size_t len, myai_manuf_t *out)
{
    if (!data || !out) return -1;
    memset(out, 0, sizeof *out);
    if (len < 12) return -1;

    out->version = data[0];
    if (out->version != 1 && out->version != 2) return -1;

    out->flags = data[1];
    out->model = (uint16_t)data[2] | ((uint16_t)data[3] << 8);
    if (!ble_scanner_is_ai_model(out->model)) return -1;

    /* Serial heuristic: if the trailing region is printable ASCII take
     * it as a string; otherwise hex-encode the last 5 bytes. */
    size_t tail_start = (len > 13) ? len - 10 : len - 5;
    if (tail_start < 4) tail_start = 4;
    size_t tail_len   = len - tail_start;
    if (tail_len > BLE_SCANNER_SERIAL_MAX - 1) tail_len = BLE_SCANNER_SERIAL_MAX - 1;

    bool all_printable = true;
    for (size_t i = 0; i < tail_len; ++i) {
        uint8_t c = data[tail_start + i];
        if (c < 0x20 || c > 0x7E) { all_printable = false; break; }
    }
    if (all_printable && tail_len >= 5) {
        memcpy(out->serial, &data[tail_start], tail_len);
        out->serial[tail_len] = '\0';
    } else {
        size_t hex_start = (len >= 5) ? len - 5 : 0;
        size_t hex_bytes = len - hex_start;
        char *p = out->serial;
        for (size_t i = 0; i < hex_bytes && (size_t)(p - out->serial) < BLE_SCANNER_SERIAL_MAX - 3; ++i) {
            snprintf(p, 3, "%02X", data[hex_start + i]);
            p += 2;
        }
        *p = '\0';
    }

    out->parsed_ok = true;
    return 0;
}

#ifdef ESP_PLATFORM

static ble_scanner_callback_t s_cb = NULL;
static void                  *s_user = NULL;
static bool                   s_initialized = false;
static bool                   s_scanning = false;

static void parse_adv_fields(const uint8_t *adv, uint8_t adv_len,
                             ble_scan_result_t *out)
{
    size_t i = 0;
    while (i + 1 < adv_len) {
        uint8_t field_len = adv[i];
        if (field_len == 0 || i + 1 + field_len > adv_len) break;
        uint8_t type = adv[i + 1];
        const uint8_t *value = &adv[i + 2];
        uint8_t vlen = field_len - 1;

        switch (type) {
            case 0x08:
            case 0x09: {
                uint8_t copy = vlen;
                if (copy > BLE_SCANNER_NAME_MAX - 1) copy = BLE_SCANNER_NAME_MAX - 1;
                memcpy(out->name, value, copy);
                out->name[copy] = '\0';
                break;
            }
            case 0xFF:
                ble_scanner_parse_manuf(value, vlen, &out->manuf);
                break;
            default:
                break;
        }
        i += 1 + field_len;
    }
}

static int gap_event_cb(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    if (event->type != BLE_GAP_EVENT_DISC) return 0;

    ble_scan_result_t result = {0};
    memcpy(result.ble_addr, event->disc.addr.val, BLE_ADDR_BYTES);
    result.ble_addr_type = (event->disc.addr.type == BLE_OWN_ADDR_RANDOM)
        ? BLE_ADDR_RANDOM : BLE_ADDR_PUBLIC;
    result.rssi = event->disc.rssi;
    parse_adv_fields(event->disc.data, event->disc.length_data, &result);

    if (result.manuf.parsed_ok && s_cb) {
        s_cb(&result, s_user);
    }
    return 0;
}

esp_err_t ble_scanner_init(void)
{
    if (s_initialized) return ESP_OK;
    s_initialized = true;
    ESP_LOGI(TAG, "init");
    return ESP_OK;
}

esp_err_t ble_scanner_start(ble_scanner_callback_t cb, void *user_ctx, uint32_t duration_ms)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (s_scanning)     return ESP_ERR_INVALID_STATE;

    s_cb = cb;
    s_user = user_ctx;

    struct ble_gap_disc_params params = {0};
    params.filter_policy   = BLE_HCI_SCAN_FILT_NO_WL;
    params.passive         = 0;
    params.filter_duplicates = 1;

    int32_t dur = (duration_ms == 0) ? BLE_HS_FOREVER : (int32_t)duration_ms;
    int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, dur, &params, gap_event_cb, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_disc failed: %d", rc);
        return ESP_FAIL;
    }
    s_scanning = true;
    ESP_LOGI(TAG, "scan started (duration_ms=%lu)", (unsigned long)duration_ms);
    return ESP_OK;
}

esp_err_t ble_scanner_stop(void)
{
    if (!s_scanning) return ESP_OK;
    int rc = ble_gap_disc_cancel();
    s_scanning = false;
    ESP_LOGI(TAG, "scan stopped (rc=%d)", rc);
    return (rc == 0 || rc == BLE_HS_EALREADY) ? ESP_OK : ESP_FAIL;
}

#endif /* ESP_PLATFORM */
