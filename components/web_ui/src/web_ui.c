/* Web UI: status page + REST endpoints. Provides everything the user
 * needs for the first bring-up: WiFi-served status page, BLE scan,
 * light registration, and power on/off. ESP-IDF only.
 *
 * Endpoints:
 *   GET  /                          HTML with Scan / Add / On / Off buttons
 *   GET  /api/status                JSON: uptime, light count, BLE state
 *   POST /api/scan                  kick off a 10s BLE scan
 *   GET  /api/scan/results          JSON list of discovered lights
 *   GET  /api/lights                JSON list of registered lights
 *   POST /api/lights                register a discovered light
 *   POST /api/lights/<id>/command   issue a light command (JSON body)
 *   GET  /api/logs                  recent event_log entries
 *   POST /api/ota                   firmware upload
 */

#ifdef ESP_PLATFORM

#include "web_ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_err.h"
#include "esp_http_server.h"
#include "esp_log.h"

#include "ble_light_client.h"
#include "ble_scanner.h"
#include "command_engine.h"
#include "event_log.h"
#include "light_registry.h"
#include "mqtt_bridge.h"
#include "ota_update.h"

static const char *TAG = "web_ui";
static httpd_handle_t s_server = NULL;

#define SCAN_BUF_MAX        16
#define SCAN_DURATION_MS    10000

static ble_scan_result_t s_scan_buf[SCAN_BUF_MAX];
static size_t            s_scan_count;

static esp_err_t send_json(httpd_req_t *req, const char *json)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

/* ===== HTML status / control page ===== */
static const char STATUS_HTML[] =
"<!doctype html><html><head><meta charset=utf-8>"
"<title>Hydra Controller</title>"
"<meta name=viewport content=\"width=device-width,initial-scale=1\">"
"<style>"
"body{font-family:system-ui,sans-serif;max-width:900px;margin:1em auto;padding:0 1em}"
"h1{font-size:1.4em}h2{font-size:1.1em;margin-top:1.5em}"
"button{padding:0.4em 0.9em;margin:0.2em;border-radius:4px;border:1px solid #888;cursor:pointer;background:#f4f4f4}"
"button:hover{background:#e6e6e6}button.primary{background:#06c;color:#fff;border-color:#048}"
"button.on{background:#3a3}button.off{background:#a33}button.on,button.off{color:#fff;border-color:#000}"
"table{border-collapse:collapse;width:100%;margin-top:0.5em}"
"th,td{text-align:left;padding:0.3em 0.6em;border-bottom:1px solid #ddd}"
"th{background:#f0f0f0}"
"code{background:#eee;padding:0.1em 0.3em;border-radius:3px;font-size:0.9em}"
".muted{color:#888;font-size:0.9em}"
"#status{padding:0.5em;background:#f8f8f8;border-radius:4px}"
"</style></head><body>"
"<h1>Hydra 64HD Controller</h1>"
"<div id=status class=muted>loading status&hellip;</div>"

"<h2>Scan &amp; add lights</h2>"
"<button class=primary onclick=startScan()>Scan (10s)</button>"
"<span id=scanmsg class=muted></span>"
"<table id=scanresults><thead><tr><th>Name</th><th>Serial</th><th>Model</th><th>RSSI</th><th>Address</th><th></th></tr></thead><tbody></tbody></table>"

"<h2>Registered lights</h2>"
"<table id=lights><thead><tr><th>Name</th><th>Serial</th><th>RSSI</th><th></th></tr></thead><tbody></tbody></table>"

"<p class=muted><a href=/api/logs>logs</a> &middot; <a href=/api/status>status JSON</a></p>"

"<script>"
"async function jget(u){return (await fetch(u)).json();}"
"async function jpost(u,b){return (await fetch(u,{method:'POST',headers:{'Content-Type':'application/json'},body:b?JSON.stringify(b):''})).json();}"
"async function refreshStatus(){"
" try{const s=await jget('/api/status');"
" document.getElementById('status').textContent="
"   `controller ready=${s.controller_ready}, registered=${s.registered_lights}, ble state=${s.ble_state||'-'}, uptime=${Math.floor(s.uptime_ms/1000)}s`;"
" }catch(e){document.getElementById('status').textContent='status fetch failed: '+e;}"
"}"
"async function refreshLights(){"
" const r=await jget('/api/lights');const tb=document.querySelector('#lights tbody');tb.innerHTML='';"
" for(const l of r.lights){const tr=document.createElement('tr');"
"  tr.innerHTML=`<td>${l.display_name}</td><td><code>${l.serial}</code></td><td>${l.last_seen_rssi}</td>"
"   <td><button class=on onclick=\"cmd('${l.light_id}','on')\">On</button>"
"   <button class=off onclick=\"cmd('${l.light_id}','off')\">Off</button></td>`;tb.appendChild(tr);}"
"}"
"async function startScan(){"
" document.getElementById('scanmsg').textContent=' scanning&hellip;';"
" await jpost('/api/scan');"
" setTimeout(refreshScanResults,11000);"
"}"
"async function refreshScanResults(){"
" const r=await jget('/api/scan/results');const tb=document.querySelector('#scanresults tbody');tb.innerHTML='';"
" for(const l of r.results){const tr=document.createElement('tr');"
"  const lid='hydra64-'+l.serial.replace(/[^a-zA-Z0-9]/g,'').slice(0,10);"
"  tr.innerHTML=`<td>${l.name||'?'}</td><td><code>${l.serial}</code></td><td>${l.model}</td><td>${l.rssi}</td><td><code>${l.ble_addr}</code></td>"
"   <td><button onclick=\"addLight('${lid}','${l.name||'Hydra'}','${l.ble_addr}','${l.serial}',${l.model})\">Add</button></td>`;tb.appendChild(tr);}"
" document.getElementById('scanmsg').textContent=` ${r.results.length} found.`;"
"}"
"async function addLight(id,name,addr,serial,model){"
" const r=await jpost('/api/lights',{light_id:id,display_name:name,ble_addr:addr,serial:serial,model:model});"
" alert('add: '+JSON.stringify(r));refreshLights();"
"}"
"async function cmd(id,power){"
" const r=await jpost('/api/lights/'+id+'/command',{power:power,timeout:60});"
" console.log('cmd',r);"
"}"
"refreshStatus();refreshLights();setInterval(refreshStatus,3000);"
"</script></body></html>";

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
        "\"ble_state\":%d,"
        "\"uptime_ms\":%lu}",
        (unsigned)light_registry_count(),
        (unsigned)group_registry_count(),
        (int)ble_light_client_current_state(),
        (unsigned long)esp_log_timestamp());
    return send_json(req, buf);
}

/* ===== scan ===== */

static void scan_cb(const ble_scan_result_t *result, void *user_ctx)
{
    (void)user_ctx;
    if (!result || !result->manuf.parsed_ok) return;
    for (size_t i = 0; i < s_scan_count; ++i) {
        if (memcmp(s_scan_buf[i].ble_addr, result->ble_addr, BLE_ADDR_BYTES) == 0) {
            s_scan_buf[i].rssi = result->rssi;
            s_scan_buf[i].manuf = result->manuf;
            return;
        }
    }
    if (s_scan_count >= SCAN_BUF_MAX) return;
    s_scan_buf[s_scan_count++] = *result;
}

static esp_err_t post_scan(httpd_req_t *req)
{
    if (ble_light_client_current_state() != BLC_STATE_IDLE) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ble busy; try again in a few seconds");
        return ESP_FAIL;
    }
    s_scan_count = 0;
    memset(s_scan_buf, 0, sizeof s_scan_buf);
    ble_scanner_init();
    esp_err_t err = ble_scanner_start(scan_cb, NULL, SCAN_DURATION_MS);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "scan start failed");
        return ESP_FAIL;
    }
    return send_json(req, "{\"started\":true,\"duration_ms\":10000}");
}

static esp_err_t get_scan_results(httpd_req_t *req)
{
    char buf[2048];
    size_t off = 0;
    off += snprintf(buf + off, sizeof buf - off, "{\"results\":[");
    for (size_t i = 0; i < s_scan_count && off < sizeof buf - 200; ++i) {
        const ble_scan_result_t *r = &s_scan_buf[i];
        char addr[24];
        snprintf(addr, sizeof addr, "%02x:%02x:%02x:%02x:%02x:%02x",
                 r->ble_addr[0], r->ble_addr[1], r->ble_addr[2],
                 r->ble_addr[3], r->ble_addr[4], r->ble_addr[5]);
        off += snprintf(buf + off, sizeof buf - off,
            "%s{\"name\":\"%s\",\"ble_addr\":\"%s\",\"rssi\":%d,"
            "\"model\":%u,\"serial\":\"%s\"}",
            i == 0 ? "" : ",",
            r->name, addr, r->rssi,
            (unsigned)r->manuf.model, r->manuf.serial);
    }
    snprintf(buf + off, sizeof buf - off, "]}");
    return send_json(req, buf);
}

/* ===== lights ===== */

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

static int parse_ble_addr(const char *s, uint8_t out[BLE_ADDR_BYTES])
{
    unsigned int b[BLE_ADDR_BYTES];
    int n = sscanf(s, "%x:%x:%x:%x:%x:%x",
                   &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]);
    if (n != BLE_ADDR_BYTES) return -1;
    for (int i = 0; i < BLE_ADDR_BYTES; ++i) out[i] = (uint8_t)b[i];
    return 0;
}

static int json_get_str(const char *json, const char *key, char *out, size_t cap)
{
    char pat[64];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *p = strstr(json, pat);
    if (!p) return -1;
    p = strchr(p + strlen(pat), ':');
    if (!p) return -1;
    ++p;
    while (*p == ' ' || *p == '\t') ++p;
    if (*p != '"') return -1;
    ++p;
    size_t i = 0;
    while (*p && *p != '"' && i + 1 < cap) { out[i++] = *p++; }
    out[i] = '\0';
    return 0;
}

static int json_get_int(const char *json, const char *key, int *out)
{
    char pat[64];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *p = strstr(json, pat);
    if (!p) return -1;
    p = strchr(p + strlen(pat), ':');
    if (!p) return -1;
    *out = (int)strtol(p + 1, NULL, 10);
    return 0;
}

static esp_err_t post_lights(httpd_req_t *req)
{
    char body[512];
    int total = 0;
    while (total < (int)sizeof body - 1) {
        int got = httpd_req_recv(req, body + total, sizeof body - 1 - total);
        if (got <= 0) break;
        total += got;
    }
    body[total < 0 ? 0 : total] = '\0';

    registered_light_t l;
    memset(&l, 0, sizeof l);
    char addr_str[24] = {0};
    int model = 335;

    if (json_get_str(body, "light_id",     l.light_id,     sizeof l.light_id) != 0 ||
        json_get_str(body, "display_name", l.display_name, sizeof l.display_name) != 0 ||
        json_get_str(body, "ble_addr",     addr_str,       sizeof addr_str) != 0 ||
        json_get_str(body, "serial",       l.serial,       sizeof l.serial) != 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing fields");
        return ESP_FAIL;
    }
    json_get_int(body, "model", &model);
    l.model = (uint16_t)model;
    l.enabled = true;
    l.ble_addr_type = BLE_ADDR_PUBLIC;
    if (parse_ble_addr(addr_str, l.ble_addr) != 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad ble_addr");
        return ESP_FAIL;
    }

    int rc = light_registry_add(&l);
    if (rc != 0) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "duplicate or full");
        return ESP_FAIL;
    }
    light_registry_save();

    char out[160];
    snprintf(out, sizeof out, "{\"added\":true,\"light_id\":\"%s\"}", l.light_id);
    return send_json(req, out);
}

static esp_err_t post_light_command(httpd_req_t *req)
{
    const char *p = strstr(req->uri, "/api/lights/");
    if (!p) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad uri"); return ESP_FAIL; }
    p += strlen("/api/lights/");
    const char *slash = strchr(p, '/');
    if (!slash) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing light_id"); return ESP_FAIL; }
    if (strcmp(slash, "/command") != 0) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "unknown light subpath");
        return ESP_FAIL;
    }
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
    cfg.max_uri_handlers = 16;
    cfg.lru_purge_enable = true;
    cfg.stack_size = 8192;

    esp_err_t err = httpd_start(&s_server, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: 0x%x", err);
        return err;
    }

    static const httpd_uri_t r_root          = { .uri = "/",                     .method = HTTP_GET,  .handler = get_root };
    static const httpd_uri_t r_status        = { .uri = "/api/status",           .method = HTTP_GET,  .handler = get_status };
    static const httpd_uri_t r_scan          = { .uri = "/api/scan",             .method = HTTP_POST, .handler = post_scan };
    static const httpd_uri_t r_scan_results  = { .uri = "/api/scan/results",     .method = HTTP_GET,  .handler = get_scan_results };
    static const httpd_uri_t r_lights_get    = { .uri = "/api/lights",           .method = HTTP_GET,  .handler = get_lights };
    static const httpd_uri_t r_lights_post   = { .uri = "/api/lights",           .method = HTTP_POST, .handler = post_lights };
    static const httpd_uri_t r_light_cmd     = { .uri = "/api/lights/*",         .method = HTTP_POST, .handler = post_light_command };
    static const httpd_uri_t r_logs          = { .uri = "/api/logs",             .method = HTTP_GET,  .handler = get_logs };
    static const httpd_uri_t r_ota           = { .uri = "/api/ota",              .method = HTTP_POST, .handler = post_ota };

    httpd_register_uri_handler(s_server, &r_root);
    httpd_register_uri_handler(s_server, &r_status);
    httpd_register_uri_handler(s_server, &r_scan);
    httpd_register_uri_handler(s_server, &r_scan_results);
    httpd_register_uri_handler(s_server, &r_lights_get);
    httpd_register_uri_handler(s_server, &r_lights_post);
    httpd_register_uri_handler(s_server, &r_light_cmd);
    httpd_register_uri_handler(s_server, &r_logs);
    httpd_register_uri_handler(s_server, &r_ota);

    ESP_LOGI(TAG, "HTTP server up on port 80");
    return ESP_OK;
}

#endif /* ESP_PLATFORM */
