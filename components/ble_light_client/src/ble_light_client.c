/* Phases 2.2-2.4 + 3.4: NimBLE central + state machine + BLE worker
 * task that drains command_queue. ESP-IDF only. */

#include "ble_light_client.h"

#ifdef ESP_PLATFORM

#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "channel_model.h"
#include "command_queue.h"
#include "event_log.h"
#include "fsci_codec.h"
#include "hydra64hd_protocol.h"
#include "light_registry.h"

static const char *TAG = "ble_light_client";

static const ble_uuid128_t SVC_UUID = BLE_UUID128_INIT(
    0xE0, 0x1C, 0x4B, 0x5E, 0x1E, 0xEB, 0xA1, 0x5C,
    0xEE, 0xF4, 0x5E, 0xBA, 0x00, 0x01, 0xFF, 0x01);
static const ble_uuid128_t CHR_RX_DATA = BLE_UUID128_INIT(
    0xE0, 0x1C, 0x4B, 0x5E, 0x1E, 0xEB, 0xA1, 0x5C,
    0xEE, 0xF4, 0x5E, 0xBA, 0x01, 0x01, 0xFF, 0x01);
static const ble_uuid128_t CHR_RX_FINAL = BLE_UUID128_INIT(
    0xE0, 0x1C, 0x4B, 0x5E, 0x1E, 0xEB, 0xA1, 0x5C,
    0xEE, 0xF4, 0x5E, 0xBA, 0x02, 0x01, 0xFF, 0x01);
static const ble_uuid128_t CHR_TX_DATA = BLE_UUID128_INIT(
    0xE0, 0x1C, 0x4B, 0x5E, 0x1E, 0xEB, 0xA1, 0x5C,
    0xEE, 0xF4, 0x5E, 0xBA, 0x03, 0x01, 0xFF, 0x01);
static const ble_uuid128_t CHR_TX_FINAL = BLE_UUID128_INIT(
    0xE0, 0x1C, 0x4B, 0x5E, 0x1E, 0xEB, 0xA1, 0x5C,
    0xEE, 0xF4, 0x5E, 0xBA, 0x04, 0x01, 0xFF, 0x01);

#define IDLE_DISCONNECT_MS    30000
#define CONNECT_TIMEOUT_MS     8000
#define CONFIRM_TIMEOUT_MS     2000
#define MTU_DEFAULT              20
#define WORKER_TICK_MS          100

typedef struct {
    blc_state_t       state;
    char              light_id[LIGHT_ID_LEN];
    uint16_t          conn_handle;
    uint16_t          attr_rx_data;
    uint16_t          attr_rx_final;
    uint16_t          attr_tx_data;
    uint16_t          attr_tx_final;
    uint16_t          mtu;
    uint16_t          pending_msg_id;
    uint64_t          last_activity_ms;
    fsci_reassembly_t rx;
    bool              confirm_received;
    pending_command_t in_flight;
} active_conn_t;

static active_conn_t s_conn;
static bool          s_started = false;
static uint16_t      s_msg_id_seq = 0x0050;
static blc_result_cb_t s_result_cb = NULL;
static void           *s_result_user = NULL;

static uint64_t now_ms(void) { return (uint64_t)esp_log_timestamp(); }

static void set_state(blc_state_t s)
{
    if (s_conn.state != s) {
        ESP_LOGI(TAG, "state %d -> %d (%s)", s_conn.state, s, s_conn.light_id);
        s_conn.state = s;
        s_conn.last_activity_ms = now_ms();
    }
}

static uint16_t next_msg_id(void)
{
    uint16_t id = s_msg_id_seq++;
    if (s_msg_id_seq == 0) s_msg_id_seq = 1;
    return id;
}

static void emit_result(const pending_command_t *cmd, bool ok, const char *msg)
{
    blc_result_t r = {0};
    strncpy(r.light_id, cmd->light_id, LIGHT_ID_LEN - 1);
    strncpy(r.command_id, cmd->command_id, CMD_ID_LEN - 1);
    r.success = ok;
    r.status  = ok ? 0 : -1;
    if (msg) strncpy(r.message, msg, sizeof r.message - 1);
    event_log_emit(ok ? EVENT_LEVEL_INFO : EVENT_LEVEL_WARN,
                   ok ? "ble_ok" : "ble_fail",
                   cmd->light_id, cmd->command_id, msg ? msg : "");
    if (s_result_cb) s_result_cb(&r, s_result_user);
}

static int on_gatt_subscribe(uint16_t conn_handle,
                             const struct ble_gatt_error *error,
                             struct ble_gatt_attr *attr, void *arg);
static int on_gatt_chr_disc(uint16_t conn_handle,
                            const struct ble_gatt_error *error,
                            const struct ble_gatt_chr *chr, void *arg);
static int on_gatt_svc_disc(uint16_t conn_handle,
                            const struct ble_gatt_error *error,
                            const struct ble_gatt_svc *svc, void *arg);
static int gap_event_cb(struct ble_gap_event *event, void *arg);

static void handle_rx_notify(const uint8_t *data, size_t len, bool is_final)
{
    fsci_frame_t f;
    int rc;
    if (is_final) {
        rc = fsci_reassembly_finalize(&s_conn.rx, data, len, &f);
    } else {
        rc = fsci_reassembly_append(&s_conn.rx, data, len);
        if (rc != FSCI_PARSE_OK) ESP_LOGW(TAG, "reassembly append failed: %d", rc);
        return;
    }
    if (rc != FSCI_PARSE_OK) {
        ESP_LOGW(TAG, "reassembly finalize failed: %d", rc);
        return;
    }
    if (s_conn.state != BLC_STATE_WAITING_CONFIRM) return;
    if (f.op_group != FSCI_OG_C2CI_CONFIRM) return;
    if (f.msg_id   != s_conn.pending_msg_id) return;
    bool ok = (f.payload_len > 0 && f.payload[0] == 0);
    emit_result(&s_conn.in_flight, ok, ok ? "confirmed" : "non-zero status");
    if (ok) light_registry_set_last_state(s_conn.in_flight.light_id, &s_conn.in_flight.state);
    s_conn.confirm_received = true;
    set_state(BLC_STATE_COOLDOWN);
}

static int gap_event_cb(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                s_conn.conn_handle = event->connect.conn_handle;
                ESP_LOGI(TAG, "connected, handle=%d", s_conn.conn_handle);
                set_state(BLC_STATE_DISCOVERING);
                ble_gattc_disc_svc_by_uuid(s_conn.conn_handle, &SVC_UUID.u,
                                           on_gatt_svc_disc, NULL);
            } else {
                ESP_LOGW(TAG, "connect failed: %d", event->connect.status);
                set_state(BLC_STATE_BACKOFF);
            }
            return 0;

        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI(TAG, "disconnected (reason 0x%x)", event->disconnect.reason);
            s_conn.conn_handle = 0;
            s_conn.attr_rx_data = s_conn.attr_rx_final = 0;
            s_conn.attr_tx_data = s_conn.attr_tx_final = 0;
            set_state(BLC_STATE_IDLE);
            return 0;

        case BLE_GAP_EVENT_MTU:
            s_conn.mtu = event->mtu.value;
            ESP_LOGI(TAG, "mtu negotiated: %u", s_conn.mtu);
            return 0;

        case BLE_GAP_EVENT_NOTIFY_RX: {
            struct os_mbuf *om = event->notify_rx.om;
            uint16_t attr = event->notify_rx.attr_handle;
            uint16_t len  = OS_MBUF_PKTLEN(om);
            uint8_t  buf[256];
            if (len > sizeof buf) len = sizeof buf;
            ble_hs_mbuf_to_flat(om, buf, len, NULL);
            bool is_final = (attr == s_conn.attr_rx_final);
            handle_rx_notify(buf, len, is_final);
            return 0;
        }

        default:
            return 0;
    }
}

static int on_gatt_svc_disc(uint16_t conn_handle,
                            const struct ble_gatt_error *error,
                            const struct ble_gatt_svc *svc, void *arg)
{
    (void)arg;
    if (error->status == BLE_HS_EDONE) {
        ble_gattc_disc_all_chrs(conn_handle, 1, 0xFFFF, on_gatt_chr_disc, NULL);
        return 0;
    }
    if (error->status != 0 || !svc) return 0;
    ESP_LOGI(TAG, "found service handle=%d-%d", svc->start_handle, svc->end_handle);
    ble_gattc_disc_all_chrs(conn_handle, svc->start_handle, svc->end_handle,
                            on_gatt_chr_disc, NULL);
    return 0;
}

static int on_gatt_chr_disc(uint16_t conn_handle,
                            const struct ble_gatt_error *error,
                            const struct ble_gatt_chr *chr, void *arg)
{
    (void)arg;
    if (error->status == BLE_HS_EDONE) {
        if (!s_conn.attr_rx_data || !s_conn.attr_rx_final ||
            !s_conn.attr_tx_data || !s_conn.attr_tx_final) {
            ESP_LOGE(TAG, "missing chars after discovery");
            ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
            set_state(BLC_STATE_ERROR);
            return 0;
        }
        set_state(BLC_STATE_SUBSCRIBING);
        static const uint8_t notify_en[] = {0x01, 0x00};
        ble_gattc_write_flat(conn_handle, s_conn.attr_rx_final + 1,
                             notify_en, sizeof notify_en,
                             on_gatt_subscribe, NULL);
        return 0;
    }
    if (error->status != 0 || !chr) return 0;

    if (ble_uuid_cmp(&chr->uuid.u, &CHR_RX_DATA.u) == 0)  s_conn.attr_rx_data  = chr->val_handle;
    if (ble_uuid_cmp(&chr->uuid.u, &CHR_RX_FINAL.u) == 0) s_conn.attr_rx_final = chr->val_handle;
    if (ble_uuid_cmp(&chr->uuid.u, &CHR_TX_DATA.u) == 0)  s_conn.attr_tx_data  = chr->val_handle;
    if (ble_uuid_cmp(&chr->uuid.u, &CHR_TX_FINAL.u) == 0) s_conn.attr_tx_final = chr->val_handle;
    return 0;
}

static int on_gatt_subscribe(uint16_t conn_handle,
                             const struct ble_gatt_error *error,
                             struct ble_gatt_attr *attr, void *arg)
{
    (void)arg; (void)attr; (void)conn_handle;
    if (error->status != 0) {
        ESP_LOGE(TAG, "subscribe failed: %d", error->status);
        set_state(BLC_STATE_ERROR);
        return 0;
    }
    static bool rx_data_subscribed = false;
    if (!rx_data_subscribed) {
        rx_data_subscribed = true;
        static const uint8_t notify_en[] = {0x01, 0x00};
        ble_gattc_write_flat(s_conn.conn_handle, s_conn.attr_rx_data + 1,
                             notify_en, sizeof notify_en,
                             on_gatt_subscribe, NULL);
        return 0;
    }
    rx_data_subscribed = false;
    set_state(BLC_STATE_READY);
    ESP_LOGI(TAG, "ready for writes");
    return 0;
}

static int write_one_chunk(uint16_t attr_handle, const uint8_t *data, size_t len)
{
    return ble_gattc_write_no_rsp_flat(s_conn.conn_handle, attr_handle, data, len);
}

static int send_frame(const uint8_t *frame, size_t frame_len)
{
    size_t mtu = (s_conn.mtu > 5) ? (s_conn.mtu - 5) : MTU_DEFAULT;
    if (frame_len <= mtu) {
        return write_one_chunk(s_conn.attr_tx_final, frame, frame_len);
    }
    size_t off = 0;
    while (frame_len - off > mtu) {
        int rc = write_one_chunk(s_conn.attr_tx_data, &frame[off], mtu);
        if (rc != 0) return rc;
        off += mtu;
    }
    return write_one_chunk(s_conn.attr_tx_final, &frame[off], frame_len - off);
}

static void issue_write(const pending_command_t *cmd)
{
    uint8_t payload[HYDRA64_SET_LDS_PAYLOAD_BYTES];
    size_t plen = hydra64_build_live_demo_scene_write(&cmd->state,
                                                      cmd->scene_timeout_sec,
                                                      payload, sizeof payload);
    if (plen == 0) { emit_result(cmd, false, "build_payload"); return; }

    uint8_t frame[128];
    size_t flen = 0;
    s_conn.pending_msg_id = next_msg_id();
    int rc = fsci_build(FSCI_OG_C2CI_REQUEST, FSCI_OC_LEGACY_SET_C2_ATTR,
                        s_conn.pending_msg_id, payload, plen,
                        frame, sizeof frame, &flen);
    if (rc != 0) { emit_result(cmd, false, "fsci_build"); return; }

    s_conn.in_flight = *cmd;
    s_conn.confirm_received = false;
    fsci_reassembly_reset(&s_conn.rx);
    set_state(BLC_STATE_WRITING);
    rc = send_frame(frame, flen);
    if (rc != 0) {
        ESP_LOGE(TAG, "send_frame failed: %d", rc);
        emit_result(cmd, false, "ble_write_failed");
        set_state(BLC_STATE_ERROR);
        return;
    }
    set_state(BLC_STATE_WAITING_CONFIRM);
}

static void try_connect_to(const registered_light_t *light)
{
    ble_addr_t peer = {0};
    peer.type = (light->ble_addr_type == BLE_ADDR_RANDOM)
        ? BLE_ADDR_RANDOM : BLE_ADDR_PUBLIC;
    for (int i = 0; i < BLE_ADDR_BYTES; ++i) {
        peer.val[i] = light->ble_addr[BLE_ADDR_BYTES - 1 - i];
    }
    strncpy(s_conn.light_id, light->light_id, LIGHT_ID_LEN - 1);
    set_state(BLC_STATE_CONNECTING);
    int rc = ble_gap_connect(BLE_OWN_ADDR_PUBLIC, &peer,
                             CONNECT_TIMEOUT_MS, NULL,
                             gap_event_cb, NULL);
    if (rc != 0) {
        ESP_LOGW(TAG, "ble_gap_connect failed: %d", rc);
        set_state(BLC_STATE_BACKOFF);
    }
}

static void worker_tick(void)
{
    uint64_t t = now_ms();

    if (s_conn.state == BLC_STATE_COOLDOWN) set_state(BLC_STATE_READY);

    if (s_conn.state == BLC_STATE_WAITING_CONFIRM &&
        t - s_conn.last_activity_ms > CONFIRM_TIMEOUT_MS) {
        ESP_LOGW(TAG, "confirm timeout for cmd %s", s_conn.in_flight.command_id);
        emit_result(&s_conn.in_flight, false, "confirm_timeout");
        set_state(BLC_STATE_ERROR);
    }

    if (s_conn.state == BLC_STATE_READY &&
        t - s_conn.last_activity_ms > IDLE_DISCONNECT_MS) {
        ESP_LOGI(TAG, "idle disconnect for %s", s_conn.light_id);
        ble_gap_terminate(s_conn.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        set_state(BLC_STATE_DISCONNECTING);
        return;
    }

    if (s_conn.state == BLC_STATE_BACKOFF || s_conn.state == BLC_STATE_ERROR) {
        if (t - s_conn.last_activity_ms > 1000) set_state(BLC_STATE_IDLE);
        return;
    }

    if (s_conn.state == BLC_STATE_IDLE) {
        for (size_t i = 0; i < light_registry_count(); ++i) {
            const registered_light_t *light = light_registry_at(i);
            if (!light || !light->enabled) continue;
            if (cmd_queue_depth(light->light_id) == 0) continue;
            try_connect_to(light);
            return;
        }
        return;
    }

    if (s_conn.state == BLC_STATE_READY && cmd_queue_depth(s_conn.light_id) > 0) {
        pending_command_t cmd;
        if (cmd_queue_pop(s_conn.light_id, &cmd) == 0) {
            issue_write(&cmd);
        }
    }
}

static void ble_worker_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "worker task started");
    for (;;) {
        worker_tick();
        vTaskDelay(pdMS_TO_TICKS(WORKER_TICK_MS));
    }
}

static void on_host_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) ESP_LOGE(TAG, "ensure_addr: %d", rc);
}

static void nimble_host_task(void *arg)
{
    (void)arg;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

esp_err_t ble_light_client_init(void)
{
    memset(&s_conn, 0, sizeof s_conn);
    s_conn.state = BLC_STATE_DISABLED;

    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init: %d", err);
        return err;
    }
    ble_hs_cfg.sync_cb = on_host_sync;
    ble_svc_gap_init();
    ble_svc_gatt_init();

    nimble_port_freertos_init(nimble_host_task);
    set_state(BLC_STATE_IDLE);
    ESP_LOGI(TAG, "init: NimBLE host running");
    return ESP_OK;
}

esp_err_t ble_light_client_start(void)
{
    if (s_started) return ESP_OK;
    BaseType_t b = xTaskCreate(ble_worker_task, "ble_worker", 5120, NULL, 5, NULL);
    if (b != pdPASS) return ESP_ERR_NO_MEM;
    s_started = true;
    return ESP_OK;
}

void ble_light_client_set_result_cb(blc_result_cb_t cb, void *user)
{
    s_result_cb = cb;
    s_result_user = user;
}

blc_state_t ble_light_client_current_state(void)
{
    return s_conn.state;
}

#endif /* ESP_PLATFORM */
