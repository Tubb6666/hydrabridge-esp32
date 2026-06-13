/* Phase 5.2: minimal web UI -- status page + REST endpoints routing
 * through command_engine (reusing mqtt_bridge parser). esp_http_server
 * on port 80. ESP-IDF only. */

#ifdef ESP_PLATFORM

#include "web_ui.h"

#include <string.h>
#include <stdio.h>

#include "esp_err.h"
#include "esp_http_server.h"
#include "esp_log.h"

#include "command_engine.h"
#include "event_log.h"
#include "light_registry.h"
#include "mqtt_bridge.h"
#include "ota_update.h"

static const char *TAG = "web_ui";
static httpd_handle_t s_server = NULL;

static esp_err_t send_json(httpd_req_t *req, const char *json)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

static const char STATUS_HTML[] =
"<!doctype html><html><head><meta charset=utf-8><title>Hydra Controller</title>"
"<style>body{font-family:system-ui,sans-serif;max-width:720px;margin:2em auto;padding:0 1em}"
"code{background:#eee;padding:0.1em 0.3em;border-radius:3px}"
"a{color:#06c;text-decoration:none}a:hover{text-decoration:underline}</style>"
"</head><body>"
"<h1>Hydra 64HD Controller</h1>"
"<p>Controller is alive. See <a href=/api/status>/api/status</a> for runtime JSON.</p>"
"<h2>API</h2><ul>"
"<li><a href=/api/status>GET /api/status</a></li>"
"<li><a href=/api/lights>GET /api/lights</a></li>"
"<li><code>POST /api/lights/&lt;light_id&gt;/command</code> &mdash; JSON body, same shape as MQTT</li>"
"<li><a href=/api/logs>GET /api/logs</a></li>"
"<li><code>POST /api/ota</code> &mdash; firmware upload</li>"
"</ul>"
"<p>See <code>docs/ble-protocol-reference.md</code> for the BLE protocol spec.</p>"
"</body></html>";

static esp_err_t get_root(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, STATUS_HTML, sizeof STATUS_HTML - 1);
}

static esp_err_t get_status(httpd_req_t *req)
{
    char buf[256];
    snprintf(buf, sizeof buf,
        "{\"controller_ready\":true,"
        "\"registered_lights\":%u,"
        "\"registered_groups\":%u,"
        "\"uptime_ms\":%lu}",
        (unsigned)light_registry_count(),
        (unsigned)group_registry_count(),
        (unsigned long)esp_log_timestamp());
    return send_json(req, buf);
}

static esp_err_t get_lights(httpd_req_t *req)
{
    char buf[1024];
    size_t off = 0;
    off += snprintf(buf + off, sizeof buf - off, "{\"lights\":[");
    size_t n = light_registry_count();
    for (size_t i = 0; i < n && off < sizeof buf - 200; ++i) {
        const registered_light_t *l = light_registry_at(i);
        if (!l) continue;
        off += snprintf(buf + off, sizeof buf - off,
            "%s{\"light_id\":\"%s\",\"display_name\":\"%s\","
            "\"serial\":\"%s\",\"model\":%u,\"enabled\":%s,"
            "\"last_seen_rssi\":%d}",
            i == 0 ? "" : ",",
            l->light_id, l->display_name, l->serial,
            (unsigned)l->model,
            l->enabled ? "true" : "false",
            l->last_seen_rssi);
    }
    snprintf(buf + off, sizeof buf - off, "]}");
    return send_json(req, buf);
}

static esp_err_t post_light_command(httpd_req_t *req)
{
    const char *p = strstr(req->uri, "/api/lights/");
    if (!p) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad uri"); return ESP_FAIL; }
    p += strlen("/api/lights/");
    const char *slash = strchr(p, '/');
    if (!slash) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing light_id"); return ESP_FAIL; }
    char light_id[LIGHT_ID_LEN];
    size_t id_len = slash - p;
    if (id_len >= LIGHT_ID_LEN) id_len = LIGHT_ID_LEN - 1;
    memcpy(light_id, p, id_len);
    light_id[id_len] = '\0';

    char body[1024];
    int total = 0;
    while (total < (int)sizeof body - 1) {
        int got = httpd_req_recv(req, body + total, sizeof body - 1 - total);
        if (got <= 0) break;
        total += got;
    }
    body[total < 0 ? 0 : total] = '\0';

    ce_request_t r;
    if (mqtt_parse_light_command(body, light_id, &r) != 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid command");
        return ESP_FAIL;
    }
    r.source = CMD_SOURCE_WEB;

    char cmd_id[CMD_ID_LEN];
    ce_result_t res = command_engine_submit(&r, cmd_id);

    char out[128];
    snprintf(out, sizeof out,
             "{\"command_id\":\"%s\",\"result\":%d}", cmd_id, (int)res);
    return send_json(req, out);
}

static esp_err_t get_logs(httpd_req_t *req)
{
    char buf[4096];
    size_t off = 0;
    off += snprintf(buf + off, sizeof buf - off, "{\"events\":[");
    size_t n = event_log_count();
    bool first = true;
    for (size_t i = 0; i < n && off < sizeof buf - 200; ++i) {
        event_log_entry_t e;
        if (!event_log_get(i, &e)) continue;
        off += snprintf(buf + off, sizeof buf - off,
            "%s{\"uptime_ms\":%llu,\"level\":%d,\"code\":\"%s\","
            "\"light_id\":\"%s\",\"command_id\":\"%s\","
            "\"message\":\"%s\"}",
            first ? "" : ",",
            (unsigned long long)e.uptime_ms,
            (int)e.level, e.code, e.light_id, e.command_id, e.message);
        first = false;
    }
    snprintf(buf + off, sizeof buf - off, "]}");
    return send_json(req, buf);
}

static esp_err_t post_ota(httpd_req_t *req)
{
    esp_err_t err = ota_update_begin();
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ota_begin");
        return ESP_FAIL;
    }
    char buf[1024];
    int received = 0;
    while (1) {
        int got = httpd_req_recv(req, buf, sizeof buf);
        if (got <= 0) break;
        err = ota_update_write(buf, got);
        if (err != ESP_OK) {
            ota_update_abort();
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ota_write");
            return ESP_FAIL;
        }
        received += got;
    }
    err = ota_update_end();
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ota_end");
        return ESP_FAIL;
    }
    char out[128];
    snprintf(out, sizeof out, "{\"received\":%d,\"status\":\"success\"}", received);
    send_json(req, out);
    return ESP_OK;
}

esp_err_t web_ui_init(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.uri_match_fn = httpd_uri_match_wildcard;
    cfg.max_uri_handlers = 12;
    cfg.lru_purge_enable = true;

    esp_err_t err = httpd_start(&s_server, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: 0x%x", err);
        return err;
    }

    static const httpd_uri_t r_root      = { .uri = "/",                     .method = HTTP_GET,  .handler = get_root };
    static const httpd_uri_t r_status    = { .uri = "/api/status",           .method = HTTP_GET,  .handler = get_status };
    static const httpd_uri_t r_lights    = { .uri = "/api/lights",           .method = HTTP_GET,  .handler = get_lights };
    static const httpd_uri_t r_light_cmd = { .uri = "/api/lights/*/command", .method = HTTP_POST, .handler = post_light_command };
    static const httpd_uri_t r_logs      = { .uri = "/api/logs",             .method = HTTP_GET,  .handler = get_logs };
    static const httpd_uri_t r_ota       = { .uri = "/api/ota",              .method = HTTP_POST, .handler = post_ota };

    httpd_register_uri_handler(s_server, &r_root);
    httpd_register_uri_handler(s_server, &r_status);
    httpd_register_uri_handler(s_server, &r_lights);
    httpd_register_uri_handler(s_server, &r_light_cmd);
    httpd_register_uri_handler(s_server, &r_logs);
    httpd_register_uri_handler(s_server, &r_ota);

    ESP_LOGI(TAG, "HTTP server up on port 80");
    return ESP_OK;
}

#endif /* ESP_PLATFORM */
