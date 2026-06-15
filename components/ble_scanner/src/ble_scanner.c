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
    if (len < 4) return -1;  /* need at least version + flags + u16 model for v1/v2; short manuf is common in pairing advs */

    out->version = data[0];
    if (out->version != 1 && out->version != 2) return -1;

    out->flags = data[1];
    out->model = (uint16_t)data[2] | ((uint16_t)data[3] << 8);

    /* Real Hydra lights, including pairing/manualDiscovery mode, advertise
     * v1/v2 manuf data with model IDs outside the
     * old 320-342 range (e.g. 94xx-99xx). Accept the structure and let the
     * serial heuristic run; upper-layer permissive scan + has_manuf_data
     * already handles discovery. is_ai_model() left for other uses. */
    /* (was: if (!ble_scanner_is_ai_model(out->model)) return -1; ) */

    /* v2 Mobius payloads place PAN ID at bytes 9..10 and the ASCII serial
     * at byte 11. Fall back to the older trailing-region heuristic for
     * shorter or non-printable formats. */
    size_t tail_start = 0;
    if (len > 11) {
        bool serial_printable = true;
        for (size_t i = 11; i < len; ++i) {
            uint8_t c = data[i];
            if (c < 0x20 || c > 0x7E) { serial_printable = false; break; }
        }
        if (serial_printable && len - 11 >= 5) {
            tail_start = 11;
        }
    }
    if (tail_start == 0) tail_start = (len > 13) ? len - 10 : len - 5;
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

static bool scanner_addr_type_from_nimble(uint8_t nimble_type, ble_addr_type_t *out)
{
    if (!out) return false;
    switch (nimble_type) {
        case BLE_ADDR_PUBLIC:
            *out = BLE_ADDR_PUBLIC;
            return true;
        case BLE_ADDR_RANDOM:
            *out = BLE_ADDR_RANDOM;
            return true;
        case BLE_ADDR_PUBLIC_ID:
            *out = BLE_ADDR_PUBLIC_ID;
            return true;
        case BLE_ADDR_RANDOM_ID:
            *out = BLE_ADDR_RANDOM_ID;
            return true;
        default:
            return false;
    }
}

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
            case 0x06:
            case 0x07: {
                /* 128-bit service UUID list (complete/incomplete). Check for Hydra service
                 * to catch pairing mode advertisements that may lack name or standard manuf data. */
                if (vlen >= 16) {
                    static const uint8_t hydra_svc[16] = {
                        0xE0, 0x1C, 0x4B, 0x5E, 0x1E, 0xEB, 0xA1, 0x5C,
                        0xEE, 0xF4, 0x5E, 0xBA, 0x00, 0x01, 0xFF, 0x01
                    };
                    if (memcmp(value, hydra_svc, 16) == 0 ||
                        (vlen > 16 && memcmp(value + (vlen - 16), hydra_svc, 16) == 0)) {
                        out->has_hydra_service = true;
                        if (out->name[0] == 0) {
                            strncpy(out->name, "MOBIUS", sizeof(out->name) - 1);
                            out->name[sizeof(out->name) - 1] = '\0';
                        }
                    }
                }
                break;
            }
            case 0xFF:
                ble_scanner_parse_manuf(value, vlen, &out->manuf);
                out->has_manuf_data = true;  /* mark even if parse_manuf didn't set parsed_ok (e.g. legacy format in pairing) */
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

    if (event->type == BLE_GAP_EVENT_DISC_COMPLETE) {
        s_scanning = false;
        ESP_LOGI(TAG, "scan complete (duration elapsed)");
        return 0;
    }

    ble_scan_result_t result = {0};
    const uint8_t *adv_data = NULL;
    uint8_t adv_len = 0;

    if (event->type == BLE_GAP_EVENT_DISC) {
        if (!scanner_addr_type_from_nimble(event->disc.addr.type, &result.ble_addr_type)) {
            return 0;
        }
        hydra_ble_addr_from_nimble(result.ble_addr, event->disc.addr.val);
        result.rssi = event->disc.rssi;
        adv_data = event->disc.data;
        adv_len = event->disc.length_data;
    } else if (event->type == BLE_GAP_EVENT_EXT_DISC) {
        /* Handle extended discovery reports (NimBLE initiates "extended discovery"
         * on this controller; pairing lights often only deliver the key v2 02 0a
         * manualDiscovery manuf data in these reports or their scan responses). */
        if (!scanner_addr_type_from_nimble(event->ext_disc.addr.type, &result.ble_addr_type)) {
            return 0;
        }
        hydra_ble_addr_from_nimble(result.ble_addr, event->ext_disc.addr.val);
        result.rssi = event->ext_disc.rssi;
        adv_data = event->ext_disc.data;
        adv_len = event->ext_disc.length_data;
    } else {
        return 0;
    }

    parse_adv_fields(adv_data, adv_len, &result);

    if (s_cb) {
        /* Forward all discoveries during user scan (short duration, small buf).
         * Upper layer (web_ui) is fully permissive for pairing mode.
         * This catches lights that only reveal the 02 0a manualDiscovery data
         * in extended reports or scan responses. */
        ESP_LOGI(TAG, "DISC: %02x:%02x:%02x:%02x:%02x:%02x rssi=%d has_manuf=%d has_hydra=%d model=%u name='%s'",
                 result.ble_addr[0], result.ble_addr[1], result.ble_addr[2],
                 result.ble_addr[3], result.ble_addr[4], result.ble_addr[5],
                 result.rssi, result.has_manuf_data, result.has_hydra_service,
                 (unsigned)result.manuf.model, result.name);
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

    /* Defensive: a prior finite-duration scan may have left s_scanning true
     * (the stack delivers DISC_COMPLETE but we now handle it; this also
     * covers any missed event or rapid re-scan requests from the web UI).
     * Force-clear so user-initiated pairing scans always succeed. */
    if (s_scanning) {
        ble_gap_disc_cancel();
        s_scanning = false;
    }

    s_cb = cb;
    s_user = user_ctx;

    struct ble_gap_disc_params params = {0};
    params.filter_policy   = BLE_HCI_SCAN_FILT_NO_WL;
    params.passive         = 0;
    params.filter_duplicates = 0;  /* report every adv during short scan to maximize chance of good manuf data */

    /* Coexistence: for short user-initiated discovery (30s), use very high duty (~100%).
     * This maximizes probability of hitting the light's sparse/irregular pairing advs (or eliciting
     * the SCAN_RSP containing the v2 02 0a manualDiscovery manuf data that the phone app sees).
     * Control path keeps low-duty. Short window + PREFER_BT keeps WiFi ok. */
    params.itvl = 0x0010;   /* ~10 ms interval */
    params.window = 0x0010; /* ~10 ms window  ~100% duty for discovery */

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
