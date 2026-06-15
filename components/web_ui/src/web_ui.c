/* Web UI: status page + REST endpoints. WiFi-served status, BLE scan,
 * light registration, commands (incl. full channels, profiles), OTA.
 * ESP-IDF only.
 *
 * Endpoints:
 *   GET  /                          HTML with Scan / Add / On / Off / Profiles
 *   GET  /api/status                JSON: uptime, light count, BLE state
 *   POST /api/scan                  kick off a 30s BLE scan (for pairing lights)
 *   GET  /api/scan/results          JSON list of discovered lights
 *   GET  /api/lights                JSON list of registered lights
 *   POST /api/lights                register a discovered light
 *   POST /api/lights/<id>/command   issue a light command (JSON body)
 *   GET  /api/logs                  recent event_log entries
 *   POST /api/ota                   firmware upload
 *   (profiles endpoints added in later phase)
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
#include "config_store.h"
#include "mqtt_bridge.h"
#include "modbus_interface.h"
#include "ota_update.h"
#include "wifi_station.h"

static const char *TAG = "web_ui";
static httpd_handle_t s_server = NULL;

#define SCAN_BUF_MAX        16
#define SCAN_DURATION_MS    30000  /* longer for pairing mode lights that may advertise sparsely or put key data in scan responses */

static ble_scan_result_t s_scan_buf[SCAN_BUF_MAX];
static size_t            s_scan_count;

static const char *channel_names[9] = {
    "brightness", "coolwhite", "blue", "deepred", "violet", "uv", "green", "royalblue", "moonlight"
};

static const user_profile_t k_builtin_profiles[] = {
    /* Values are 0..1000. Source percentages were multiplied by 10.
     * 105%/110% HD values are clamped to the current channel model max. */
    { "Zoa Pop",        "Best for zoas, mushrooms, LPS viewing, and fluorescence.",       { 1000, 150, 1000,  50, 1000,  950,  50, 1000,   0 } },
    { "AB Plus",        "Good balanced profile for mixed reef growth and color.",          { 1000, 250, 1000, 100,  900,  900, 100, 1000,   0 } },
    { "Mixed Reef",     "More natural look while still reef-safe.",                        { 1000, 400,  900, 100,  800,  750, 150,  900,   0 } },
    { "Frag Growth",    "Higher blue and violet, moderate white for frag racks.",          { 1000, 200, 1000,  50,  900,  850,  50, 1000,   0 } },
    { "Evening",        "After-work viewing without blasting PAR.",                        { 1000,  50,  650,   0,  550,  450,   0,  700,   0 } },
    { "Photo Mode",     "Better for coral photos with less overwhelming blue.",            { 1000, 450,  750,  80,  500,  400, 100,  750,   0 } },
    { "Moonlight Only", "Very low moonlight for a short night window.",                    { 1000,   0,    0,   0,    0,    0,   0,    0,  20 } },
};

static const size_t k_builtin_profile_count =
    sizeof(k_builtin_profiles) / sizeof(k_builtin_profiles[0]);

static const user_profile_t *builtin_profile_by_name(const char *name)
{
    if (!name) return NULL;
    for (size_t i = 0; i < k_builtin_profile_count; ++i) {
        if (strcmp(k_builtin_profiles[i].name, name) == 0) {
            return &k_builtin_profiles[i];
        }
    }
    return NULL;
}

static void merge_scan_result(ble_scan_result_t *dst, const ble_scan_result_t *src)
{
    if (!dst || !src) return;
    dst->rssi = src->rssi;
    dst->ble_addr_type = src->ble_addr_type;
    if (src->name[0]) {
        strncpy(dst->name, src->name, sizeof dst->name - 1);
        dst->name[sizeof dst->name - 1] = '\0';
    }
    if (src->has_manuf_data) {
        dst->has_manuf_data = true;
        dst->manuf = src->manuf;
    }
    if (src->has_hydra_service) {
        dst->has_hydra_service = true;
        if (dst->name[0] == '\0') {
            strncpy(dst->name, "MOBIUS", sizeof dst->name - 1);
            dst->name[sizeof dst->name - 1] = '\0';
        }
    }
}

static void refresh_registered_peer(const registered_light_t *candidate, int8_t rssi)
{
    if (!candidate) return;
    const registered_light_t *existing = NULL;
    if (candidate->serial[0]) {
        existing = light_registry_get_by_serial(candidate->serial);
    }
    if (!existing) {
        existing = light_registry_get(candidate->light_id);
    }
    if (!existing) {
        existing = light_registry_get_by_addr(candidate->ble_addr);
    }
    if (existing &&
        light_registry_update_discovery(existing->light_id, candidate->ble_addr,
                                        candidate->ble_addr_type, rssi) == 0) {
        light_registry_save();
    }
}

static esp_err_t send_json(httpd_req_t *req, const char *json)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

/* ===== HTML status / control page ===== */
static const char STATUS_HTML[] =
"<!doctype html><html><head><meta charset=utf-8>"
"<title>HydraBridge ESP32</title>"
"<meta name=viewport content=\"width=device-width,initial-scale=1\">"
"<style>"
"*{box-sizing:border-box}"
":root{color-scheme:light;--bg:#f5f7fb;--panel:#fff;--ink:#172033;--muted:#667085;--line:#d9e0ea;--blue:#1f6feb;--blue2:#dbeafe;--green:#12805c;--red:#c0352b;--amber:#b7791f;--shadow:0 14px 40px rgba(24,36,56,.09)}"
"body{margin:0;background:linear-gradient(180deg,#eef4ff 0,#f5f7fb 310px);color:var(--ink);font-family:Inter,ui-sans-serif,system-ui,-apple-system,BlinkMacSystemFont,\"Segoe UI\",sans-serif}"
"main{max-width:1180px;margin:0 auto;padding:26px 18px 36px}"
"header{display:grid;grid-template-columns:1fr auto;gap:18px;align-items:end;margin-bottom:18px}"
"h1{font-size:clamp(1.8rem,4vw,3.1rem);line-height:1;margin:0;letter-spacing:0;font-weight:780}"
"h2{font-size:1rem;margin:0;font-weight:760}"
"p{margin:.4rem 0;color:var(--muted)}"
"a{color:var(--blue);text-decoration:none}a:hover{text-decoration:underline}"
".eyebrow{font-size:.74rem;font-weight:800;text-transform:uppercase;letter-spacing:.08em;color:#2e5aac;margin-bottom:.5rem}"
".subhead{max-width:650px;font-size:1rem}"
".toplinks{display:flex;gap:8px;flex-wrap:wrap;justify-content:flex-end}"
".linkbtn,.btn{appearance:none;border:1px solid var(--line);background:#fff;color:var(--ink);border-radius:8px;padding:.58rem .8rem;min-height:38px;font-weight:720;font-size:.9rem;cursor:pointer;box-shadow:none}"
".linkbtn{display:inline-flex;align-items:center}.btn:hover,.linkbtn:hover{border-color:#a9b7cc;background:#f8fbff;text-decoration:none}"
".btn.primary{background:var(--blue);border-color:var(--blue);color:#fff}.btn.primary:hover{background:#1558c8}"
".btn.good{background:var(--green);border-color:var(--green);color:#fff}.btn.danger{background:#fff1f0;border-color:#f0b8b2;color:#9f2018}.btn.ghost{background:transparent}"
".btn:disabled{opacity:.55;cursor:not-allowed}"
".stats{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:10px;margin:18px 0}"
".stat{background:rgba(255,255,255,.86);border:1px solid rgba(203,213,225,.8);border-radius:8px;padding:13px 14px;box-shadow:0 8px 24px rgba(24,36,56,.06)}"
".tabs{display:flex;gap:8px;flex-wrap:wrap;margin:16px 0}.tab{appearance:none;border:1px solid var(--line);background:#fff;color:var(--muted);border-radius:8px;padding:.68rem .95rem;min-height:40px;font-weight:800;cursor:pointer}.tab.active{background:var(--ink);border-color:var(--ink);color:#fff}.tabpane{display:none}.tabpane.active{display:block}.tabgrid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:14px;align-items:start}.tabgrid.single{grid-template-columns:1fr}"
".label{display:block;color:var(--muted);font-size:.72rem;font-weight:800;text-transform:uppercase;letter-spacing:.07em}"
".value{display:block;font-size:1.35rem;font-weight:790;margin-top:5px}.value.small{font-size:1rem}"
".grid{display:grid;grid-template-columns:minmax(0,1.15fr) minmax(330px,.85fr);gap:14px;align-items:start}"
".panel{background:var(--panel);border:1px solid var(--line);border-radius:8px;box-shadow:var(--shadow);overflow:hidden}"
".panel-head{display:flex;align-items:center;justify-content:space-between;gap:12px;padding:15px 16px;border-bottom:1px solid #e7ecf3;background:#fbfdff}"
".panel-body{padding:14px 16px}.stack{display:grid;gap:10px}"
".row{display:grid;grid-template-columns:1fr auto;gap:12px;align-items:center;border:1px solid #e6edf5;border-radius:8px;padding:12px;background:#fff}"
".row-title{display:flex;align-items:center;gap:9px;flex-wrap:wrap}.name{font-size:1rem;font-weight:780}"
".meta{display:flex;gap:8px;flex-wrap:wrap;margin-top:6px;color:var(--muted);font-size:.83rem}"
".desc{color:var(--muted);font-size:.86rem;line-height:1.35;margin-top:6px;max-width:52rem}"
".pill{display:inline-flex;align-items:center;gap:5px;border:1px solid #d9e3ef;background:#f8fafc;border-radius:999px;padding:.2rem .5rem;color:#475467;font-size:.78rem;font-weight:700}"
".dot{width:9px;height:9px;border-radius:999px;background:#98a2b3}.dot.ready{background:#12b76a}.dot.busy{background:#f59e0b}.dot.err{background:#e5483f}"
".actions{display:flex;gap:7px;flex-wrap:wrap;justify-content:flex-end}.split{display:flex;gap:8px;align-items:center;flex-wrap:wrap}"
"input,select,textarea{width:100%;border:1px solid #cfd8e5;border-radius:8px;padding:.55rem .65rem;min-height:38px;background:#fff;color:var(--ink);font:inherit}textarea{min-height:72px;resize:vertical}"
"input[type=number]{max-width:88px}input[type=range]{padding:0;min-height:24px;accent-color:var(--blue)}"
".rename{display:grid;grid-template-columns:minmax(140px,230px) auto;gap:7px;margin-top:9px}"
".empty{border:1px dashed #cbd5e1;border-radius:8px;padding:22px;text-align:center;color:var(--muted);background:#f8fafc}"
".scanbar{height:8px;border-radius:999px;background:#e7edf7;overflow:hidden;margin-top:10px}.scanbar span{display:block;height:100%;width:0;background:linear-gradient(90deg,#1f6feb,#14b8a6);transition:width .2s}"
".channels{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:10px;margin-top:12px}.channel{border:1px solid #e6edf5;border-radius:8px;padding:10px;background:#fbfdff}.channel b{display:block;font-size:.78rem;margin-bottom:8px}"
".settings{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:10px}.settings .wide{grid-column:1/-1}.check{display:flex;align-items:center;gap:9px;border:1px solid #e6edf5;border-radius:8px;padding:10px;background:#fbfdff}.check input{width:auto;min-height:auto}"
".profile-list{display:grid;gap:8px;margin-top:12px}.profile-bars{display:grid;grid-template-columns:repeat(9,1fr);gap:3px;margin-top:8px}.bar{height:28px;background:#dbeafe;border-radius:4px;position:relative;overflow:hidden}.bar span{position:absolute;left:0;right:0;bottom:0;background:#1f6feb}"
".toast{position:fixed;right:16px;bottom:16px;max-width:360px;background:#172033;color:#fff;padding:12px 14px;border-radius:8px;box-shadow:var(--shadow);font-weight:700;opacity:0;transform:translateY(8px);pointer-events:none;transition:.18s}.toast.show{opacity:1;transform:none}"
"@media(max-width:860px){header{grid-template-columns:1fr}.toplinks{justify-content:flex-start}.stats{grid-template-columns:repeat(2,minmax(0,1fr))}.grid,.tabgrid{grid-template-columns:1fr}.row{grid-template-columns:1fr}.actions{justify-content:flex-start}.channels{grid-template-columns:1fr 1fr}}"
"@media(max-width:520px){main{padding:18px 12px 28px}.stats{grid-template-columns:1fr}.panel-head{align-items:flex-start;flex-direction:column}.channels,.settings{grid-template-columns:1fr}.rename{grid-template-columns:1fr}.btn,.linkbtn{width:100%;justify-content:center}.actions,.split{width:100%}}"
"</style></head><body>"
"<main>"
"<header><div><div class=eyebrow>Open-source light gateway</div><h1>HydraBridge ESP32</h1><p class=subhead>Local control for AquaIllumination Hydra lights over RS485 and MQTT.</p></div><nav class=toplinks><a class=linkbtn href=/api/logs>Logs</a><a class=linkbtn href=/api/status>Status JSON</a></nav></header>"
"<section class=stats><div class=stat><span class=label>Controller</span><span id=ready class=value>--</span></div><div class=stat><span class=label>BLE State</span><span id=bleState class=\"value small\">--</span></div><div class=stat><span class=label>Lights</span><span id=lightCount class=value>0</span></div><div class=stat><span class=label>Uptime</span><span id=uptime class=\"value small\">--</span></div></section>"
"<nav class=tabs><button class=\"tab active\" data-tab=lights>Lights</button><button class=tab data-tab=profiles>Profiles</button><button class=tab data-tab=settings>Settings</button></nav>"
"<section id=tab-lights class=\"tabpane active\"><div class=tabgrid>"
"<section class=panel><div class=panel-head><div><h2>Registered Lights</h2><p>Operate paired lights and update names.</p></div><button id=refreshLights class=\"btn ghost\">Refresh</button></div><div id=lights class=\"panel-body stack\"><div class=empty>Loading lights&hellip;</div></div></section>"
"<section class=panel><div class=panel-head><div><h2>Discovery</h2><p>Scan for nearby Mobius devices and add new lights.</p></div><button id=scanBtn class=\"btn primary\">Scan and Auto-Add</button></div><div class=panel-body><div id=scanmsg class=meta>Ready to scan for 30 seconds.</div><div class=scanbar><span id=scanProgress></span></div><div id=scanresults class=\"stack\" style=\"margin-top:12px\"></div></div></section>"
"</div><section class=panel style=\"margin-top:14px\"><div class=panel-head><div><h2>Light Groups</h2><p>Create groups for applying profiles to multiple lights together.</p></div><button id=refreshGroups class=\"btn ghost\">Refresh</button></div><div class=panel-body><div class=stack><label><span class=label>Group Name</span><input id=groupName maxlength=32 placeholder=\"Display tank\"></label><div id=groupMembers class=stack></div><div class=split><button id=saveGroup class=\"btn primary\">Save Group</button></div><div id=groups class=stack></div></div></div></section></section>"
"<section id=tab-profiles class=tabpane><div class=tabgrid>"
"<section class=panel><div class=panel-head><div><h2>Profile Builder</h2><p>Save channel mixes and apply them to a selected light or group.</p></div></div><div class=panel-body><div class=stack><label><span class=label>Target</span><select id=targetSelect></select></label><label><span class=label>Profile Name</span><input id=profname maxlength=16 value=reef-day></label><label><span class=label>Description</span><textarea id=profdesc maxlength=128 placeholder=\"What this profile is for\"></textarea></label><div class=split><button id=saveProfile class=\"btn primary\">Save Profile</button><button id=applyCurrent class=btn>Apply Profile</button><button id=intUp class=btn>+ Intensity</button><button id=intDown class=btn>- Intensity</button></div></div><div id=channels class=channels></div></div></section>"
"<section class=panel><div class=panel-head><div><h2>Saved Profiles</h2><p>Apply, edit, or delete stored mixes.</p></div></div><div class=panel-body><div id=profiles class=profile-list><div class=empty>Loading profiles&hellip;</div></div></div></section>"
"</div></section>"
"<section id=tab-settings class=tabpane><div class=tabgrid>"
"<section class=panel><div class=panel-head><div><h2>WiFi</h2><p>Connect to your home network or use setup hotspot fallback.</p></div><span id=wifiState class=pill>Loading</span></div><div class=panel-body><div class=settings><label class=\"check wide\"><input id=wifiEnabled type=checkbox><span><b>Enable WiFi station</b><br><span class=meta>Connects to the configured home WiFi.</span></span></label><label><span class=label>SSID</span><input id=wifiSsid maxlength=32 autocomplete=off></label><label><span class=label>Password</span><input id=wifiPass maxlength=64 type=password autocomplete=current-password></label><label class=\"check wide\"><input id=wifiApFallback type=checkbox><span><b>Setup hotspot fallback</b><br><span class=meta>Starts setup AP if no WiFi is configured or connection fails.</span></span></label><label><span class=label>Setup AP Name</span><input id=wifiApSsid maxlength=32></label><label><span class=label>Setup AP Password</span><input id=wifiApPass maxlength=64 type=password></label><div class=\"split wide\"><button id=saveWifi class=\"btn primary\">Save WiFi</button><span class=meta>Setup URL: http://192.168.1.10/</span></div></div></div></section>"
"<section class=panel><div class=panel-head><div><h2>RS485 Slave</h2><p>Allow another controller to command registered lights over Modbus RTU.</p></div><span id=rs485State class=pill>Loading</span></div><div class=panel-body><div class=settings><label class=\"check wide\"><input id=mbEnabled type=checkbox><span><b>Enable RS485 slave</b><br><span class=meta>Off by default. Saves to NVS.</span></span></label><label><span class=label>Slave Address</span><input id=mbAddr type=number min=1 max=247></label><label><span class=label>Baud Rate</span><input id=mbBaud type=number min=1200 max=921600 step=100></label><label><span class=label>Parity</span><select id=mbParity><option value=0>None</option><option value=1>Even</option><option value=2>Odd</option></select></label><label><span class=label>UART Port</span><input id=mbUart type=number min=0 max=2></label><label><span class=label>TX GPIO</span><input id=mbTx type=number min=-1 max=48></label><label><span class=label>RX GPIO</span><input id=mbRx type=number min=-1 max=48></label><label><span class=label>DE/RTS GPIO</span><input id=mbDe type=number min=-1 max=48></label><div class=\"split wide\"><button id=saveModbus class=\"btn primary\">Save RS485</button><span class=meta>Default wiring: RX 18, TX 17.</span></div></div></div></section>"
"<section class=panel><div class=panel-head><div><h2>MQTT</h2><p>Connect HydraBridge to a local MQTT broker.</p></div><span id=mqttState class=pill>Loading</span></div><div class=panel-body><div class=settings><label class=\"check wide\"><input id=mqEnabled type=checkbox><span><b>Enable MQTT</b><br><span class=meta>Off by default. Saves to NVS.</span></span></label><label><span class=label>Broker Host</span><input id=mqHost maxlength=64 placeholder=\"192.168.1.10\"></label><label><span class=label>Port</span><input id=mqPort type=number min=1 max=65535></label><label><span class=label>Username</span><input id=mqUser maxlength=32 autocomplete=username></label><label><span class=label>Password</span><input id=mqPass maxlength=64 type=password autocomplete=current-password></label><label><span class=label>Client ID</span><input id=mqClient maxlength=32></label><label><span class=label>Keepalive Sec</span><input id=mqKeepalive type=number min=15 max=3600></label><label><span class=label>Base Topic</span><input id=mqBase maxlength=32></label><label><span class=label>HA Discovery Prefix</span><input id=mqHaPrefix maxlength=32></label><label class=\"check wide\"><input id=mqTls type=checkbox><span><b>Use TLS</b><br><span class=meta>Uses mqtts on the selected port.</span></span></label><label class=\"check wide\"><input id=mqHa type=checkbox><span><b>Home Assistant friendly</b><br><span class=meta>Publish MQTT discovery entities when connected.</span></span></label><div class=\"split wide\"><button id=saveMqtt class=\"btn primary\">Save MQTT</button><span class=meta>Commands use base/controller/light/&lt;id&gt;/set and group/&lt;id&gt;/set.</span></div></div></div></section>"
"</div></section><div id=toast class=toast></div></main>"
"<script>"
"const $=id=>document.getElementById(id);"
"let appLights=[];let appGroups=[];"
"const channelNames=['brightness','coolwhite','blue','deepred','violet','uv','green','royalblue','moonlight'];"
"const channelLabels=['Brightness','Cool White','Blue','Deep Red','Violet','UV','Green','Royal Blue','Moonlight'];"
"const stateNames={0:'Disabled',1:'Idle',3:'Connecting',4:'Discovering',5:'Subscribing',7:'Ready',8:'Writing',9:'Waiting Confirm',10:'Cooldown',11:'Disconnecting',12:'Error',13:'Backoff'};"
"function modelLabel(m){const t={320:'Prime HD',321:'Prime Freshwater',322:'Prime 16HD',335:'Hydra 64HD',336:'Hydra 32HD',337:'Hydra 26HD',338:'Blade'};return t[m]||('AI Light '+m);}"
"function el(tag,cls,txt){const n=document.createElement(tag);if(cls)n.className=cls;if(txt!==undefined)n.textContent=txt;return n;}"
"function toast(msg){const t=$('toast');t.textContent=msg;t.classList.add('show');clearTimeout(window.toastTimer);window.toastTimer=setTimeout(()=>t.classList.remove('show'),2600);}"
"async function fetchJson(u,opt){const r=await fetch(u,opt);if(!r.ok)throw new Error(await r.text()||r.statusText);return r.json();}"
"const jget=u=>fetchJson(u);"
"const jpost=(u,b)=>fetchJson(u,{method:'POST',headers:{'Content-Type':'application/json'},body:b?JSON.stringify(b):''});"
"const jdel=u=>fetchJson(u,{method:'DELETE'});"
"function fmtUptime(ms){let s=Math.floor((ms||0)/1000),h=Math.floor(s/3600),m=Math.floor((s%3600)/60);return h?`${h}h ${m}m`:`${m}m ${s%60}s`;}"
"function bleDot(st){if(st===7)return'ready';if(st===12||st===13)return'err';if(st&&st!==1)return'busy';return'';}"
"async function refreshStatus(){try{const s=await jget('/api/status');$('ready').textContent=s.controller_ready?'Ready':'Offline';$('lightCount').textContent=s.registered_lights||0;$('uptime').textContent=fmtUptime(s.uptime_ms);$('bleState').textContent=stateNames[s.ble_state]||String(s.ble_state);}catch(e){$('ready').textContent='Error';$('bleState').textContent='Status failed';}}"
"function addMeta(parent,items){const m=el('div','meta');for(const it of items){const p=el('span','pill',it);m.appendChild(p);}parent.appendChild(m);}"
"function refreshTargetSelect(){const sel=$('targetSelect');if(!sel)return;const old=sel.value;sel.innerHTML='';for(const l of appLights){const o=document.createElement('option');o.value='light:'+l.light_id;o.textContent='Light: '+(l.display_name||l.light_id);sel.appendChild(o);}for(const g of appGroups){const o=document.createElement('option');o.value='group:'+g.group_id;o.textContent='Group: '+(g.display_name||g.group_id);sel.appendChild(o);}if(old)sel.value=old;if(!sel.value&&sel.options.length)sel.selectedIndex=0;}"
"function renderGroupMembers(){const box=$('groupMembers');if(!box)return;box.innerHTML='';if(!appLights.length){box.appendChild(el('div','empty','Add lights before creating a group.'));return;}for(const l of appLights){const lab=el('label','check');const cb=document.createElement('input');cb.type='checkbox';cb.value=l.light_id;cb.className='groupMember';lab.appendChild(cb);lab.appendChild(el('span',null,l.display_name||l.light_id));box.appendChild(lab);}}"
"async function refreshLights(){try{const r=await jget('/api/lights');appLights=r.lights||[];const box=$('lights');box.innerHTML='';if(!appLights.length){box.appendChild(el('div','empty','No registered lights yet. Start a discovery scan to add one.'));}else{for(const l of appLights)box.appendChild(lightRow(l));}renderGroupMembers();refreshTargetSelect();}catch(e){toast('Lights failed: '+e.message);}}"
"function lightRow(l){const row=el('article','row');const left=el('div');const title=el('div','row-title');const dot=el('span','dot');title.appendChild(dot);title.appendChild(el('span','name',l.display_name||l.light_id));title.appendChild(el('span','pill',l.enabled?'Enabled':'Disabled'));left.appendChild(title);addMeta(left,[modelLabel(l.model),l.serial||'No serial','RSSI '+l.last_seen_rssi,l.light_id]);const ren=el('div','rename');const inp=document.createElement('input');inp.value=l.display_name||'';inp.maxLength=32;inp.setAttribute('aria-label','Light name');const save=el('button','btn','Save Name');save.onclick=()=>renameLight(l.light_id,inp.value);ren.appendChild(inp);ren.appendChild(save);left.appendChild(ren);const act=el('div','actions');[['On',()=>cmd(l.light_id,'on'),'good'],['Off',()=>cmd(l.light_id,'off'),'danger'],['+ Int',()=>intensity(l.light_id,50),''],['- Int',()=>intensity(l.light_id,-50),''],['Remove',()=>removeLight(l.light_id),'ghost']].forEach(a=>{const b=el('button','btn '+a[2],a[0]);b.onclick=a[1];act.appendChild(b);});row.appendChild(left);row.appendChild(act);return row;}"
"async function renameLight(id,name){const clean=(name||'').trim();if(!clean)return toast('Name is required');if(/[\"\\\\<>]/.test(clean))return toast('Name cannot contain quotes, backslashes, <, or >');try{await jpost('/api/lights/'+id+'/rename',{display_name:clean});toast('Light renamed');refreshLights();}catch(e){toast('Rename failed: '+e.message);}}"
"async function removeLight(id){if(!confirm('Remove '+id+'?'))return;try{await jdel('/api/lights/'+id);toast('Light removed');refreshLights();}catch(e){toast('Remove failed: '+e.message);}}"
"async function refreshGroups(){try{const r=await jget('/api/groups');appGroups=r.groups||[];const box=$('groups');box.innerHTML='';if(!appGroups.length){box.appendChild(el('div','empty','No groups yet. Select lights above and save a group.'));}else{for(const g of appGroups)box.appendChild(groupRow(g));}refreshTargetSelect();}catch(e){toast('Groups failed: '+e.message);}}"
"function groupRow(g){const row=el('article','row');const left=el('div');const title=el('div','row-title');title.appendChild(el('span','name',g.display_name||g.group_id));title.appendChild(el('span','pill',(g.member_count||0)+' lights'));left.appendChild(title);addMeta(left,(g.members||[]).map(m=>'Light '+m));const act=el('div','actions');const del=el('button','btn danger','Delete');del.onclick=()=>deleteGroup(g.group_id);act.appendChild(del);row.appendChild(left);row.appendChild(act);return row;}"
"async function saveGroup(){const name=($('groupName').value||'').trim();if(!name)return toast('Group name is required');if(/[\"\\\\<>]/.test(name))return toast('Group name cannot contain quotes, backslashes, <, or >');const members=[...document.querySelectorAll('.groupMember:checked')].map(c=>c.value);if(!members.length)return toast('Select at least one light');const gid='grp-'+name.replace(/[^a-zA-Z0-9]/g,'').slice(0,20);try{await jpost('/api/groups',{group_id:gid,display_name:name,members:members.join(',')});toast('Group saved');$('groupName').value='';document.querySelectorAll('.groupMember').forEach(c=>c.checked=false);refreshGroups();}catch(e){toast('Group save failed: '+e.message);}}"
"async function deleteGroup(id){if(!confirm('Delete group '+id+'?'))return;try{await jdel('/api/groups/'+id);toast('Group deleted');refreshGroups();}catch(e){toast('Delete group failed: '+e.message);}}"
"async function startScan(){const btn=$('scanBtn'),msg=$('scanmsg'),bar=$('scanProgress');btn.disabled=true;bar.style.width='0';try{const d=await jpost('/api/scan');const dur=(d&&d.duration_ms)||30000;const start=Date.now();msg.textContent='Scanning for nearby Mobius lights...';clearInterval(window.scanTimer);window.scanTimer=setInterval(()=>{const pct=Math.min(100,((Date.now()-start)/dur)*100);bar.style.width=pct+'%';},250);setTimeout(refreshScanResults,dur+1200);}catch(e){btn.disabled=false;msg.textContent='Scan failed';toast('Scan failed: '+e.message);}}"
"async function refreshScanResults(){clearInterval(window.scanTimer);$('scanProgress').style.width='100%';$('scanBtn').disabled=false;try{const r=await jget('/api/scan/results');const box=$('scanresults');box.innerHTML='';const list=r.results||[];$('scanmsg').textContent=list.length?`${list.length} device${list.length===1?'':'s'} found.`:'No devices found. Put the light in pairing mode and scan again.';for(const l of list)box.appendChild(scanRow(l));refreshLights();}catch(e){toast('Scan results failed: '+e.message);}}"
"function scanRow(l){let serialForLid=l.serial&&l.serial.length>0&&l.model!=0?l.serial:l.ble_addr.replace(/:/g,'').toUpperCase().slice(0,10);const lid='hydra64-'+serialForLid.replace(/[^a-zA-Z0-9]/g,'').slice(0,10);let label=l.model==0?'Unknown pairing device':modelLabel(l.model);const shown=l.name||label,useModel=l.model||335,useSerial=l.serial&&l.serial.length>0?l.serial:l.ble_addr;const row=el('article','row');const left=el('div');const title=el('div','row-title');title.appendChild(el('span','name',shown));title.appendChild(el('span','pill',label));left.appendChild(title);addMeta(left,[useSerial,l.ble_addr+' t='+l.ble_addr_type,'RSSI '+l.rssi]);const act=el('div','actions');const b=el('button','btn primary','Add Light');b.onclick=()=>addLight(lid,shown,l.ble_addr,l.ble_addr_type,useSerial,useModel);act.appendChild(b);row.appendChild(left);row.appendChild(act);return row;}"
"async function addLight(id,name,addr,addr_type,serial,model){try{const r=await jpost('/api/lights',{light_id:id,display_name:name,ble_addr:addr,ble_addr_type:addr_type,serial:serial,model:model});toast(r.added?'Light added':'Light already registered');refreshLights();}catch(e){toast('Add failed: '+e.message);}}"
"async function cmd(id,power){try{await jpost('/api/lights/'+id+'/command',{power:power,timeout:60});toast(power==='on'?'Power on queued':'Power off queued');}catch(e){toast('Command failed: '+e.message);}}"
"async function intensity(id,delta){try{await jpost('/api/lights/'+id+'/command',{intensity_delta:delta,timeout:60});toast('Intensity command queued');}catch(e){toast('Intensity failed: '+e.message);}}"
"function currentTarget(){const v=$('targetSelect').value||'';const p=v.split(':');return{type:p[0]||'',id:p.slice(1).join(':')};}"
"function targetUrl(t){return t.type==='group'?'/api/groups/'+t.id+'/command':'/api/lights/'+t.id+'/command';}"
"function intensityDelta(d){const t=currentTarget();if(t.id)intensityTarget(t,d);else toast('Select a target first');}"
"function buildChannels(){const box=$('channels');box.innerHTML='';channelNames.forEach((n,i)=>{const c=el('label','channel');c.appendChild(el('b',null,channelLabels[i]));const range=document.createElement('input');range.type='range';range.min=0;range.max=1000;range.value=i===0?1000:0;range.id='ch'+i;const num=document.createElement('input');num.type='number';num.min=0;num.max=1000;num.value=range.value;range.oninput=()=>num.value=range.value;num.oninput=()=>range.value=Math.max(0,Math.min(1000,parseInt(num.value)||0));c.appendChild(range);c.appendChild(num);box.appendChild(c);});}"
"function profileBody(name){const desc=($('profdesc').value||'').trim();const b={name:name,description:desc};for(let j=0;j<9;j++)b[channelNames[j]]=parseInt($('ch'+j).value)||0;return b;}"
"async function saveProfile(){const name=($('profname').value||'profile').trim();const desc=($('profdesc').value||'').trim();if(!name)return toast('Profile name is required');if(/[\"\\\\<>]/.test(name+desc))return toast('Profile text cannot contain quotes, backslashes, <, or >');try{await jpost('/api/profiles',profileBody(name));toast('Profile saved');refreshProfiles();}catch(e){toast('Save failed: '+e.message);}}"
"async function refreshProfiles(){try{const r=await jget('/api/profiles');const box=$('profiles');box.innerHTML='';const ps=r.profiles||[];if(!ps.length){box.appendChild(el('div','empty','No profiles saved yet.'));return;}for(const pr of ps)box.appendChild(profileRow(pr));}catch(e){toast('Profiles failed: '+e.message);}}"
"function profileRow(pr){const row=el('article','row');const left=el('div');const title=el('div','row-title');title.appendChild(el('span','name',pr.name));if(pr.builtin)title.appendChild(el('span','pill','Built-in'));left.appendChild(title);if(pr.description)left.appendChild(el('div','desc',pr.description));const bars=el('div','profile-bars');(pr.intensities||[]).forEach(v=>{const b=el('div','bar');const s=document.createElement('span');s.style.height=Math.max(3,Math.min(100,Math.round((v||0)/10)))+'%';b.appendChild(s);bars.appendChild(b);});left.appendChild(bars);const act=el('div','actions');let actions=[['Apply',()=>applyProfile(pr.name),'primary'],['Load',()=>loadProfile(pr),'']];if(!pr.builtin)actions.push(['Delete',()=>delProfile(pr.name),'danger']);actions.forEach(a=>{const b=el('button','btn '+a[2],a[0]);b.onclick=a[1];act.appendChild(b);});row.appendChild(left);row.appendChild(act);return row;}"
"function loadProfile(pr){$('profname').value=pr.name;$('profdesc').value=pr.description||'';(pr.intensities||[]).forEach((v,i)=>{const r=$('ch'+i);if(r){r.value=v;const n=r.parentNode.querySelector('input[type=number]');if(n)n.value=v;}});toast('Profile loaded');}"
"async function applyProfile(name){const t=currentTarget();if(!t.id)return toast('Select a target first');try{await jpost(targetUrl(t),{profile:name,timeout:60});toast(t.type==='group'?'Group profile queued':'Profile queued');}catch(e){toast('Apply failed: '+e.message);}}"
"async function intensityTarget(t,delta){try{await jpost(targetUrl(t),{intensity_delta:delta,timeout:60});toast('Intensity command queued');}catch(e){toast('Intensity failed: '+e.message);}}"
"async function delProfile(name){if(!confirm('Delete '+name+'?'))return;try{await jdel('/api/profiles/'+encodeURIComponent(name));toast('Profile deleted');refreshProfiles();}catch(e){toast('Delete failed: '+e.message);}}"
"function wifiModeLabel(m){return({0:'Off',1:'Station',2:'Setup AP',3:'Station + AP'})[m]||String(m);}"
"async function refreshWifiConfig(){try{const r=await jget('/api/config/wifi');$('wifiEnabled').checked=!!r.enabled;$('wifiSsid').value=r.ssid||'';$('wifiPass').value=r.password_set?'********':'';$('wifiApFallback').checked=!!r.ap_fallback_enabled;$('wifiApSsid').value=r.ap_ssid||'HydraBridge-Setup';$('wifiApPass').value=r.ap_password_set?'********':'';$('wifiState').textContent=wifiModeLabel(r.mode)+(r.connected?' Connected':'');}catch(e){$('wifiState').textContent='Config failed';toast('WiFi config failed: '+e.message);}}"
"async function saveWifiConfig(){const pass=$('wifiPass').value,apPass=$('wifiApPass').value;const b={enabled:$('wifiEnabled').checked,ssid:($('wifiSsid').value||'').trim(),ap_fallback_enabled:$('wifiApFallback').checked,ap_ssid:($('wifiApSsid').value||'HydraBridge-Setup').trim()};if(pass!=='********')b.password=pass;if(apPass!=='********')b.ap_password=apPass;try{await jpost('/api/config/wifi',b);toast('WiFi settings saved');refreshWifiConfig();refreshStatus();}catch(e){toast('WiFi save failed: '+e.message);}}"
"function modbusStatusLabel(s){return({0:'Disabled',1:'Slave Ready',2:'Master Ready',3:'Error'})[s]||String(s);}"
"async function refreshModbusConfig(){try{const r=await jget('/api/config/modbus');$('mbEnabled').checked=!!r.enabled;$('mbAddr').value=r.slave_address;$('mbBaud').value=r.baud_rate;$('mbParity').value=r.parity;$('mbUart').value=r.uart_port;$('mbTx').value=r.tx_pin;$('mbRx').value=r.rx_pin;$('mbDe').value=r.rts_de_pin;$('rs485State').textContent=modbusStatusLabel(r.status)+(r.running?'':'');}catch(e){$('rs485State').textContent='Config failed';toast('RS485 config failed: '+e.message);}}"
"async function saveModbusConfig(){const b={enabled:$('mbEnabled').checked,slave_address:parseInt($('mbAddr').value)||10,baud_rate:parseInt($('mbBaud').value)||19200,parity:parseInt($('mbParity').value)||0,uart_port:parseInt($('mbUart').value)||1,tx_pin:parseInt($('mbTx').value),rx_pin:parseInt($('mbRx').value),rts_de_pin:parseInt($('mbDe').value)};try{const r=await jpost('/api/config/modbus',b);toast(r.applied?'RS485 settings applied':'RS485 settings saved');refreshModbusConfig();refreshStatus();}catch(e){toast('RS485 save failed: '+e.message);}}"
"function mqttStatusLabel(s){return({0:'Disabled',1:'Disconnected',2:'Connected'})[s]||String(s);}"
"async function refreshMqttConfig(){try{const r=await jget('/api/config/mqtt');$('mqEnabled').checked=!!r.enabled;$('mqHost').value=r.host||'';$('mqPort').value=r.port||1883;$('mqTls').checked=!!r.use_tls;$('mqUser').value=r.username||'';$('mqPass').value=r.password_set?'********':'';$('mqClient').value=r.client_id||'';$('mqKeepalive').value=r.keepalive_sec||60;$('mqBase').value=r.base_topic||'aihydra';$('mqHa').checked=!!r.home_assistant_discovery;$('mqHaPrefix').value=r.home_assistant_prefix||'homeassistant';$('mqttState').textContent=mqttStatusLabel(r.status);}catch(e){$('mqttState').textContent='Config failed';toast('MQTT config failed: '+e.message);}}"
"async function saveMqttConfig(){const pass=$('mqPass').value;const b={enabled:$('mqEnabled').checked,host:($('mqHost').value||'').trim(),port:parseInt($('mqPort').value)||1883,use_tls:$('mqTls').checked,username:($('mqUser').value||'').trim(),client_id:($('mqClient').value||'').trim(),keepalive_sec:parseInt($('mqKeepalive').value)||60,base_topic:($('mqBase').value||'aihydra').trim(),home_assistant_discovery:$('mqHa').checked,home_assistant_prefix:($('mqHaPrefix').value||'homeassistant').trim()};if(pass!=='********')b.password=pass;try{const r=await jpost('/api/config/mqtt',b);toast(r.applied?'MQTT settings applied':'MQTT settings saved');refreshMqttConfig();refreshStatus();}catch(e){toast('MQTT save failed: '+e.message);}}"
"$('scanBtn').onclick=startScan;$('refreshLights').onclick=refreshLights;$('saveProfile').onclick=saveProfile;$('applyCurrent').onclick=()=>applyProfile(($('profname').value||'').trim());$('intUp').onclick=()=>intensityDelta(50);$('intDown').onclick=()=>intensityDelta(-50);$('saveWifi').onclick=saveWifiConfig;$('saveModbus').onclick=saveModbusConfig;$('saveMqtt').onclick=saveMqttConfig;"
"$('refreshGroups').onclick=refreshGroups;$('saveGroup').onclick=saveGroup;document.querySelectorAll('.tab').forEach(b=>b.onclick=()=>{document.querySelectorAll('.tab').forEach(x=>x.classList.remove('active'));document.querySelectorAll('.tabpane').forEach(x=>x.classList.remove('active'));b.classList.add('active');$('tab-'+b.dataset.tab).classList.add('active');});"
"buildChannels();refreshStatus();refreshLights();refreshGroups();refreshProfiles();refreshWifiConfig();refreshModbusConfig();refreshMqttConfig();setInterval(refreshStatus,3000);setInterval(refreshProfiles,10000);setInterval(refreshWifiConfig,10000);setInterval(refreshModbusConfig,10000);setInterval(refreshMqttConfig,10000);"
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

static bool build_hydra_light_from_result(const ble_scan_result_t *r, registered_light_t *out);

static void scan_cb(const ble_scan_result_t *result, void *user_ctx)
{
    (void)user_ctx;
    if (!result) return;

    /* Claim hydra lights *immediately* on sighting during the user scan, but ONLY
     * when the report actually carried the manufacturer data (has_manuf=1, v1/v2).
     * Observed pairing/manualDiscovery identification is from the 0xFF manuf
     * payload (v2 02 + flags bit 3 for manualDiscovery), not just the service UUID.
     * We stop the scan and connect immediately with the fresh addr from *that* report
     * to capture the current (possibly RPA) addr before it rotates or the pairing
     * adv window closes. This is why phone finds it fast (gets full scanRecord with
     * both adv + rsp in one go) but delayed connect after full scan fails.
     * Only claims if new (add succeeds); dups just continue (or update buf). */
    registered_light_t l;
    if (result->has_manuf_data && build_hydra_light_from_result(result, &l)) {
        int rc = light_registry_add(&l);
        if (rc == 0) {
            light_registry_save();
            ble_scanner_stop();
            try_connect_to(&l, false);  /* hot connect: skip extra prime, use the sighting itself as the recent SCAN_REQ */
            /* fall through so this sighting is still recorded in the scan results */
        } else {
            refresh_registered_peer(&l, result->rssi);
        }
    }

    /* For user discovery scan (30s, max 16 results): be extremely permissive.
     * Show ANY BLE device seen so that lights in weird pairing mode (no standard name,
     * no parsable 0xFF, no service UUID in adv) still appear by their addr/rssi.
     * User picks the right one (strong RSSI, known addr) and clicks Add.
     * (Normal non-pairing lights will have name/MOBIUS/manuf anyway.) */
    // no looks_like filter - accept all
    // (dedup and max still apply)
    for (size_t i = 0; i < s_scan_count; ++i) {
        if (memcmp(s_scan_buf[i].ble_addr, result->ble_addr, BLE_ADDR_BYTES) == 0) {
            merge_scan_result(&s_scan_buf[i], result);
            return;
        }
    }
    if (s_scan_count >= SCAN_BUF_MAX) return;
    s_scan_buf[s_scan_count++] = *result;
}

static esp_err_t post_scan(httpd_req_t *req)
{
    int st = ble_light_client_current_state();
    /* Allow user scans for *new* pairing lights even if a previously-registered
     * light is in BACKOFF/ERROR (dead light, pairing mode, etc.). Only block
     * during active link use. The long backoff in light_client prevents spam. */
    if (st == BLC_STATE_CONNECTING || st == BLC_STATE_DISCOVERING ||
        st == BLC_STATE_WRITING || st == BLC_STATE_WAITING_CONFIRM) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ble busy; try again in a few seconds");
        return ESP_FAIL;
    }
    s_scan_count = 0;
    memset(s_scan_buf, 0, sizeof s_scan_buf);
    ble_scanner_init();
    ble_scanner_stop();  /* ensure any prior scan flag is cleared before starting fresh user scan */
    esp_err_t err = ble_scanner_start(scan_cb, NULL, SCAN_DURATION_MS);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "scan start failed");
        return ESP_FAIL;
    }
    return send_json(req, "{\"started\":true,\"duration_ms\":30000}");
}

static bool looks_like_hydra(const ble_scan_result_t *r)
{
    if (!r) return false;
    if (r->has_hydra_service) return true;
    if (r->has_manuf_data) {
        if (r->manuf.version == 1 || r->manuf.version == 2) return true;
        if (ble_scanner_is_ai_model(r->manuf.model)) return true;
    }
    if (strstr(r->name, "MOBIUS") || strstr(r->name, "Hydra")) return true;
    return false;
}

static bool build_hydra_light_from_result(const ble_scan_result_t *r, registered_light_t *out)
{
    if (!r || !out || !looks_like_hydra(r)) return false;
    memset(out, 0, sizeof(*out));
    out->enabled = true;
    out->ble_addr_type = r->ble_addr_type;
    memcpy(out->ble_addr, r->ble_addr, sizeof out->ble_addr);

    uint16_t m = r->manuf.model ? r->manuf.model : 335;
    out->model = m;

    char base[32] = {0};
    if (r->manuf.serial[0] && m != 0) {
        strncpy(base, r->manuf.serial, sizeof(base)-1);
    } else {
        for (int j = 0; j < 6; j++) {
            char tmp[3] = {0};
            snprintf(tmp, 3, "%02X", r->ble_addr[j]);
            strncat(base, tmp, sizeof(base) - strlen(base) - 1);
        }
        if (strlen(base) > 10) base[10] = 0;
    }

    // clean to alnum only (match JS derivation for lid)
    char cleaned[32] = {0};
    size_t ci = 0;
    for (char *p = base; *p && ci < sizeof(cleaned)-1; p++) {
        unsigned char c = (unsigned char)*p;
        if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
            cleaned[ci++] = *p;
        }
    }

    snprintf(out->light_id, sizeof out->light_id, "hydra64-%.20s", cleaned);

    if (r->name[0]) {
        strncpy(out->display_name, r->name, sizeof out->display_name - 1);
    } else {
        snprintf(out->display_name, sizeof out->display_name,
                 (m == 0 ? "Unknown (pairing)" : "Hydra %u"), (unsigned)m);
    }

    if (r->manuf.serial[0] && m != 0) {
        strncpy(out->serial, r->manuf.serial, sizeof out->serial - 1);
    } else {
        hydra_ble_addr_format(r->ble_addr, out->serial, sizeof out->serial);
    }
    return true;
}

static void auto_add_hydra_lights(void)
{
    for (size_t i = 0; i < s_scan_count; ++i) {
        const ble_scan_result_t *r = &s_scan_buf[i];
        registered_light_t l;
        if (build_hydra_light_from_result(r, &l)) {
            int rc = light_registry_add(&l);
            if (rc == 0) {
                light_registry_save();
                try_connect_to(&l, true);
            } else {
                refresh_registered_peer(&l, r->rssi);
            }
        }
    }
}

static esp_err_t get_scan_results(httpd_req_t *req)
{
    auto_add_hydra_lights();  /* automatically register & connect any hydra lights found in this scan (pairing or not) */

    char buf[4096];
    size_t off = 0;
    off += snprintf(buf + off, sizeof buf - off, "{\"results\":[");
    for (size_t i = 0; i < s_scan_count && off < sizeof buf - 200; ++i) {
        const ble_scan_result_t *r = &s_scan_buf[i];
        char addr[24];
        hydra_ble_addr_format(r->ble_addr, addr, sizeof addr);
        off += snprintf(buf + off, sizeof buf - off,
            "%s{\"name\":\"%s\",\"ble_addr\":\"%s\",\"ble_addr_type\":%d,\"rssi\":%d,"
            "\"model\":%u,\"serial\":\"%s\"}",
            i == 0 ? "" : ",",
            r->name, addr, (int)r->ble_addr_type, r->rssi,
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
    ++p;
    while (*p == ' ' || *p == '\t') ++p;
    if (!((*p >= '0' && *p <= '9') || *p == '-')) return -1;
    *out = (int)strtol(p, NULL, 10);
    return 0;
}

static int json_get_bool(const char *json, const char *key, bool *out)
{
    char pat[64];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *p = strstr(json, pat);
    if (!p) return -1;
    p = strchr(p + strlen(pat), ':');
    if (!p) return -1;
    ++p;
    while (*p == ' ' || *p == '\t') ++p;
    if (strncmp(p, "true", 4) == 0) {
        *out = true;
        return 0;
    }
    if (strncmp(p, "false", 5) == 0) {
        *out = false;
        return 0;
    }
    int v = 0;
    if (json_get_int(json, key, &v) == 0) {
        *out = (v != 0);
        return 0;
    }
    return -1;
}

static bool safe_display_name(const char *s)
{
    if (!s || !s[0]) return false;
    for (const char *p = s; *p; ++p) {
        if (*p == '"' || *p == '\\' || *p == '<' || *p == '>') return false;
    }
    return true;
}

static bool safe_profile_text(const char *s, bool allow_empty)
{
    if (!s) return false;
    if (!allow_empty && !s[0]) return false;
    for (const char *p = s; *p; ++p) {
        unsigned char c = (unsigned char)*p;
        if (c < 0x20 || *p == '"' || *p == '\\' || *p == '<' || *p == '>') {
            return false;
        }
    }
    return true;
}

static bool safe_mqtt_text(const char *s, bool allow_empty, bool allow_slash)
{
    if (!s) return false;
    if (!allow_empty && !s[0]) return false;
    for (const char *p = s; *p; ++p) {
        unsigned char c = (unsigned char)*p;
        if (c < 0x20 || c > 0x7e) return false;
        if (*p == '"' || *p == '\\' || *p == '<' || *p == '>') return false;
        if (*p == '#' || *p == '+') return false;
        if (!allow_slash && *p == '/') return false;
    }
    return true;
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
    int addr_type = BLE_ADDR_PUBLIC;
    json_get_int(body, "ble_addr_type", &addr_type);
    if (addr_type == BLE_ADDR_RANDOM) {
        l.ble_addr_type = BLE_ADDR_RANDOM;
    } else if (addr_type == BLE_ADDR_PUBLIC_ID) {
        l.ble_addr_type = BLE_ADDR_PUBLIC_ID;
    } else if (addr_type == BLE_ADDR_RANDOM_ID) {
        l.ble_addr_type = BLE_ADDR_RANDOM_ID;
    } else {
        l.ble_addr_type = BLE_ADDR_PUBLIC;
    }
    if (hydra_ble_addr_parse(addr_str, l.ble_addr) != 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad ble_addr");
        return ESP_FAIL;
    }

    int rc = light_registry_add(&l);
    if (rc != 0) {
        const registered_light_t *existing = light_registry_get(l.light_id);
        if (!existing) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "duplicate or full");
            return ESP_FAIL;
        }
        if (light_registry_update_discovery(existing->light_id, l.ble_addr,
                                            l.ble_addr_type, l.last_seen_rssi) != 0) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "update failed");
            return ESP_FAIL;
        }
    }
    light_registry_save();

    /* For lights added from a fresh scan result (especially pairing mode with
     * the v2 02 0a manualDiscovery manuf), immediately prime + connect using
     * the *exact* addr we just received. This keeps the attempt as close as
     * possible to the scan time (when the light was advertising the pairing
     * data and accepting connects). The normal worker will take over once
     * connected. This is why "hard-coded" worked but delayed worker-driven
     * connect after UI add sometimes didn't (addr rotation / narrow window). */
    try_connect_to(&l, true);

    char out[160];
    snprintf(out, sizeof out, "{\"added\":%s,\"light_id\":\"%s\"}",
             rc == 0 ? "true" : "false", l.light_id);
    return send_json(req, out);
}

static esp_err_t delete_light(httpd_req_t *req)
{
    const char *p = strstr(req->uri, "/api/lights/");
    if (!p) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad uri"); return ESP_FAIL; }
    p += strlen("/api/lights/");
    char light_id[LIGHT_ID_LEN];
    size_t i = 0;
    while (p[i] && p[i] != '/' && i + 1 < LIGHT_ID_LEN) { light_id[i] = p[i]; ++i; }
    light_id[i] = '\0';
    if (i == 0) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing light_id"); return ESP_FAIL; }

    int rc = light_registry_remove(light_id);
    if (rc != 0) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "not found");
        return ESP_FAIL;
    }
    light_registry_save();
    char out[96];
    snprintf(out, sizeof out, "{\"removed\":true,\"light_id\":\"%s\"}", light_id);
    return send_json(req, out);
}

/* ===== groups ===== */

static esp_err_t get_groups(httpd_req_t *req)
{
    char buf[2048];
    size_t off = 0;
    off += snprintf(buf + off, sizeof buf - off, "{\"groups\":[");
    size_t n = group_registry_count();
    for (size_t i = 0; i < n && off < sizeof buf - 300; ++i) {
        const light_group_t *g = group_registry_at(i);
        if (!g) continue;
        off += snprintf(buf + off, sizeof buf - off,
            "%s{\"group_id\":\"%s\",\"display_name\":\"%s\",\"enabled\":%s,"
            "\"member_count\":%u,\"members\":[",
            i == 0 ? "" : ",", g->group_id, g->display_name,
            g->enabled ? "true" : "false", (unsigned)g->member_count);
        for (uint8_t m = 0; m < g->member_count && off < sizeof buf - 80; ++m) {
            off += snprintf(buf + off, sizeof buf - off,
                            "%s\"%s\"", m == 0 ? "" : ",", g->light_ids[m]);
        }
        off += snprintf(buf + off, sizeof buf - off, "]}");
    }
    snprintf(buf + off, sizeof buf - off, "]}");
    return send_json(req, buf);
}

static void make_group_id_from_name(const char *name, char out[GROUP_ID_LEN])
{
    strncpy(out, "grp-", GROUP_ID_LEN - 1);
    size_t off = 4;
    for (const char *p = name; p && *p && off + 1 < GROUP_ID_LEN; ++p) {
        unsigned char c = (unsigned char)*p;
        if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
            out[off++] = (char)c;
        }
    }
    out[off] = '\0';
}

static esp_err_t post_groups(httpd_req_t *req)
{
    char body[512];
    int total = 0;
    while (total < (int)sizeof body - 1) {
        int got = httpd_req_recv(req, body + total, sizeof body - 1 - total);
        if (got <= 0) break;
        total += got;
    }
    body[total < 0 ? 0 : total] = '\0';

    light_group_t g;
    memset(&g, 0, sizeof g);
    g.enabled = true;
    g.fanout_mode = LIGHT_FANOUT_SEQUENTIAL;

    if (json_get_str(body, "display_name", g.display_name, sizeof g.display_name) != 0 ||
        !safe_display_name(g.display_name)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad display_name");
        return ESP_FAIL;
    }
    if (json_get_str(body, "group_id", g.group_id, sizeof g.group_id) != 0 || g.group_id[0] == '\0') {
        make_group_id_from_name(g.display_name, g.group_id);
    }
    if (!safe_profile_text(g.group_id, false)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad group_id");
        return ESP_FAIL;
    }

    char members[256] = {0};
    if (json_get_str(body, "members", members, sizeof members) != 0 || members[0] == '\0') {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing members");
        return ESP_FAIL;
    }
    char *save = NULL;
    for (char *tok = strtok_r(members, ",", &save);
         tok && g.member_count < LIGHT_GROUP_MEMBERS_MAX;
         tok = strtok_r(NULL, ",", &save)) {
        while (*tok == ' ' || *tok == '\t') ++tok;
        if (!light_registry_get(tok)) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "member light not found");
            return ESP_FAIL;
        }
        strncpy(g.light_ids[g.member_count], tok, LIGHT_ID_LEN - 1);
        g.light_ids[g.member_count][LIGHT_ID_LEN - 1] = '\0';
        g.member_count++;
    }
    if (g.member_count == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty group");
        return ESP_FAIL;
    }

    if (group_registry_get(g.group_id)) {
        group_registry_remove(g.group_id);
    }
    if (group_registry_add(&g) != 0) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "group add failed");
        return ESP_FAIL;
    }
    light_registry_save();
    char out[128];
    snprintf(out, sizeof out, "{\"saved\":true,\"group_id\":\"%s\"}", g.group_id);
    return send_json(req, out);
}

static esp_err_t delete_group(httpd_req_t *req)
{
    const char *p = strstr(req->uri, "/api/groups/");
    if (!p) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad uri"); return ESP_FAIL; }
    p += strlen("/api/groups/");
    char group_id[GROUP_ID_LEN];
    size_t i = 0;
    while (p[i] && p[i] != '/' && i + 1 < GROUP_ID_LEN) { group_id[i] = p[i]; ++i; }
    group_id[i] = '\0';
    if (i == 0) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing group_id"); return ESP_FAIL; }
    if (group_registry_remove(group_id) != 0) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "not found");
        return ESP_FAIL;
    }
    light_registry_save();
    char out[96];
    snprintf(out, sizeof out, "{\"deleted\":true,\"group_id\":\"%s\"}", group_id);
    return send_json(req, out);
}

/* ===== RS485 / Modbus config ===== */

static const char *parity_name(modbus_parity_t p)
{
    switch (p) {
        case MODBUS_PARITY_EVEN: return "even";
        case MODBUS_PARITY_ODD:  return "odd";
        case MODBUS_PARITY_NONE:
        default:                 return "none";
    }
}

static esp_err_t get_modbus_config(httpd_req_t *req)
{
    config_modbus_t mb;
    config_store_load_modbus(&mb);
    char buf[384];
    snprintf(buf, sizeof buf,
        "{\"enabled\":%s,"
        "\"running\":%s,"
        "\"status\":%u,"
        "\"slave_address\":%u,"
        "\"baud_rate\":%lu,"
        "\"data_bits\":%u,"
        "\"parity\":%d,"
        "\"parity_name\":\"%s\","
        "\"stop_bits\":%u,"
        "\"uart_port\":%u,"
        "\"tx_pin\":%d,"
        "\"rx_pin\":%d,"
        "\"rts_de_pin\":%d,"
        "\"response_timeout_ms\":%lu,"
        "\"command_watchdog_ms\":%lu}",
        mb.enabled ? "true" : "false",
        modbus_slave_driver_is_running() ? "true" : "false",
        (unsigned)modbus_store_get(HYDRA_MODBUS_REG_MODBUS_STATUS),
        (unsigned)mb.slave_address,
        (unsigned long)mb.baud_rate,
        (unsigned)mb.data_bits,
        (int)mb.parity,
        parity_name(mb.parity),
        (unsigned)mb.stop_bits,
        (unsigned)mb.uart_port,
        (int)mb.tx_pin,
        (int)mb.rx_pin,
        (int)mb.rts_de_pin,
        (unsigned long)mb.response_timeout_ms,
        (unsigned long)mb.command_watchdog_ms);
    return send_json(req, buf);
}

static bool valid_gpio_or_disabled(int pin)
{
    return pin == -1 || (pin >= 0 && pin <= 48);
}

static esp_err_t post_modbus_config(httpd_req_t *req)
{
    char body[512];
    int total = 0;
    while (total < (int)sizeof body - 1) {
        int got = httpd_req_recv(req, body + total, sizeof body - 1 - total);
        if (got <= 0) break;
        total += got;
    }
    body[total < 0 ? 0 : total] = '\0';

    config_modbus_t mb;
    config_store_load_modbus(&mb);

    bool enabled = false;
    if (json_get_bool(body, "enabled", &enabled) == 0) mb.enabled = enabled;

    int v = 0;
    if (json_get_int(body, "slave_address", &v) == 0) mb.slave_address = (uint8_t)v;
    if (json_get_int(body, "baud_rate", &v) == 0) mb.baud_rate = (uint32_t)v;
    if (json_get_int(body, "parity", &v) == 0) mb.parity = (modbus_parity_t)v;
    if (json_get_int(body, "uart_port", &v) == 0) mb.uart_port = (uint8_t)v;
    if (json_get_int(body, "tx_pin", &v) == 0) mb.tx_pin = (int8_t)v;
    if (json_get_int(body, "rx_pin", &v) == 0) mb.rx_pin = (int8_t)v;
    if (json_get_int(body, "rts_de_pin", &v) == 0) mb.rts_de_pin = (int8_t)v;
    mb.master_mode_enabled = false;
    mb.data_bits = 8;
    mb.stop_bits = 1;

    if (mb.slave_address < 1 || mb.slave_address > 247) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "slave_address must be 1..247");
        return ESP_FAIL;
    }
    if (mb.baud_rate < 1200 || mb.baud_rate > 921600) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "baud_rate out of range");
        return ESP_FAIL;
    }
    if (!(mb.parity == MODBUS_PARITY_NONE || mb.parity == MODBUS_PARITY_EVEN ||
          mb.parity == MODBUS_PARITY_ODD)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad parity");
        return ESP_FAIL;
    }
    if (mb.uart_port > 2) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "uart_port must be 0..2");
        return ESP_FAIL;
    }
    if (!valid_gpio_or_disabled(mb.tx_pin) || !valid_gpio_or_disabled(mb.rx_pin) ||
        !valid_gpio_or_disabled(mb.rts_de_pin)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "pin must be -1 or 0..48");
        return ESP_FAIL;
    }
    if (mb.enabled && (mb.tx_pin < 0 || mb.rx_pin < 0 || mb.rts_de_pin < 0)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "enabled RS485 requires tx_pin, rx_pin, and rts_de_pin");
        return ESP_FAIL;
    }

    esp_err_t err = config_store_save_modbus(&mb);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "save failed");
        return ESP_FAIL;
    }

    err = modbus_interface_reconfigure();
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "reconfigure failed");
        return ESP_FAIL;
    }

    char out[128];
    snprintf(out, sizeof out,
             "{\"saved\":true,\"applied\":true,\"enabled\":%s,\"running\":%s,\"status\":%u}",
             mb.enabled ? "true" : "false",
             modbus_slave_driver_is_running() ? "true" : "false",
             (unsigned)modbus_store_get(HYDRA_MODBUS_REG_MODBUS_STATUS));
    return send_json(req, out);
}

/* ===== WiFi config ===== */

static esp_err_t get_wifi_config(httpd_req_t *req)
{
    config_wifi_t w;
    config_store_load_wifi(&w);

    char buf[512];
    snprintf(buf, sizeof buf,
        "{\"enabled\":%s,"
        "\"connected\":%s,"
        "\"ap_active\":%s,"
        "\"mode\":%d,"
        "\"ssid\":\"%s\","
        "\"password_set\":%s,"
        "\"ap_fallback_enabled\":%s,"
        "\"ap_ssid\":\"%s\","
        "\"ap_password_set\":%s,"
        "\"setup_url\":\"http://192.168.1.10/\"}",
        w.enabled ? "true" : "false",
        hydra_wifi_is_connected() ? "true" : "false",
        hydra_wifi_ap_is_active() ? "true" : "false",
        (int)hydra_wifi_mode(),
        w.ssid,
        w.password[0] ? "true" : "false",
        w.ap_fallback_enabled ? "true" : "false",
        w.ap_ssid,
        w.ap_password[0] ? "true" : "false");
    return send_json(req, buf);
}

static esp_err_t post_wifi_config(httpd_req_t *req)
{
    char body[768];
    int total = 0;
    while (total < (int)sizeof body - 1) {
        int got = httpd_req_recv(req, body + total, sizeof body - 1 - total);
        if (got <= 0) break;
        total += got;
    }
    body[total < 0 ? 0 : total] = '\0';

    config_wifi_t w;
    config_store_load_wifi(&w);

    bool b = false;
    if (json_get_bool(body, "enabled", &b) == 0) w.enabled = b;
    if (json_get_bool(body, "ap_fallback_enabled", &b) == 0) w.ap_fallback_enabled = b;

    char s[CONFIG_WIFI_PSK_LEN];
    if (json_get_str(body, "ssid", s, sizeof s) == 0) {
        strncpy(w.ssid, s, sizeof w.ssid - 1);
        w.ssid[sizeof w.ssid - 1] = '\0';
    }
    if (json_get_str(body, "password", s, sizeof s) == 0 && strcmp(s, "********") != 0) {
        strncpy(w.password, s, sizeof w.password - 1);
        w.password[sizeof w.password - 1] = '\0';
    }
    if (json_get_str(body, "ap_ssid", s, sizeof s) == 0) {
        strncpy(w.ap_ssid, s, sizeof w.ap_ssid - 1);
        w.ap_ssid[sizeof w.ap_ssid - 1] = '\0';
    }
    if (json_get_str(body, "ap_password", s, sizeof s) == 0 && strcmp(s, "********") != 0) {
        strncpy(w.ap_password, s, sizeof w.ap_password - 1);
        w.ap_password[sizeof w.ap_password - 1] = '\0';
    }

    if (w.ap_fallback_enabled && w.ap_ssid[0] == '\0') {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "AP fallback requires ap_ssid");
        return ESP_FAIL;
    }
    if (!safe_profile_text(w.ssid, true) ||
        !safe_profile_text(w.password, true) ||
        !safe_profile_text(w.ap_ssid, !w.ap_fallback_enabled) ||
        !safe_profile_text(w.ap_password, true)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad WiFi text");
        return ESP_FAIL;
    }
    if (w.ap_password[0] && strlen(w.ap_password) < 8) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "AP password must be blank or at least 8 chars");
        return ESP_FAIL;
    }

    esp_err_t err = config_store_save_wifi(&w);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "save failed");
        return ESP_FAIL;
    }
    err = hydra_wifi_reconfigure();
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "reconfigure failed");
        return ESP_FAIL;
    }

    char out[160];
    snprintf(out, sizeof out,
             "{\"saved\":true,\"applied\":true,\"connected\":%s,\"ap_active\":%s,\"mode\":%d}",
             hydra_wifi_is_connected() ? "true" : "false",
             hydra_wifi_ap_is_active() ? "true" : "false",
             (int)hydra_wifi_mode());
    return send_json(req, out);
}

/* ===== MQTT config ===== */

static esp_err_t get_mqtt_config(httpd_req_t *req)
{
    config_mqtt_t mq;
    config_store_load_mqtt(&mq);

    uint16_t status = modbus_store_get(HYDRA_MODBUS_REG_MQTT_STATUS);
    char buf[640];
    snprintf(buf, sizeof buf,
        "{\"enabled\":%s,"
        "\"connected\":%s,"
        "\"status\":%u,"
        "\"host\":\"%s\","
        "\"port\":%u,"
        "\"use_tls\":%s,"
        "\"username\":\"%s\","
        "\"password_set\":%s,"
        "\"client_id\":\"%s\","
        "\"keepalive_sec\":%u,"
        "\"base_topic\":\"%s\","
        "\"home_assistant_discovery\":%s,"
        "\"home_assistant_prefix\":\"%s\"}",
        mq.enabled ? "true" : "false",
        mqtt_bridge_is_connected() ? "true" : "false",
        (unsigned)status,
        mq.host,
        (unsigned)mq.port,
        mq.use_tls ? "true" : "false",
        mq.username,
        mq.password[0] ? "true" : "false",
        mq.client_id,
        (unsigned)mq.keepalive_sec,
        mq.base_topic,
        mq.home_assistant_discovery ? "true" : "false",
        mq.home_assistant_prefix);
    return send_json(req, buf);
}

static esp_err_t post_mqtt_config(httpd_req_t *req)
{
    char body[1024];
    int total = 0;
    while (total < (int)sizeof body - 1) {
        int got = httpd_req_recv(req, body + total, sizeof body - 1 - total);
        if (got <= 0) break;
        total += got;
    }
    body[total < 0 ? 0 : total] = '\0';

    config_mqtt_t mq;
    config_store_load_mqtt(&mq);

    bool b = false;
    if (json_get_bool(body, "enabled", &b) == 0) mq.enabled = b;
    if (json_get_bool(body, "use_tls", &b) == 0) mq.use_tls = b;
    if (json_get_bool(body, "home_assistant_discovery", &b) == 0) {
        mq.home_assistant_discovery = b;
    }

    char s[CONFIG_MQTT_PSK_LEN];
    if (json_get_str(body, "host", s, sizeof s) == 0) {
        strncpy(mq.host, s, sizeof mq.host - 1);
        mq.host[sizeof mq.host - 1] = '\0';
    }
    if (json_get_str(body, "username", s, sizeof s) == 0) {
        strncpy(mq.username, s, sizeof mq.username - 1);
        mq.username[sizeof mq.username - 1] = '\0';
    }
    if (json_get_str(body, "password", s, sizeof s) == 0 && strcmp(s, "********") != 0) {
        strncpy(mq.password, s, sizeof mq.password - 1);
        mq.password[sizeof mq.password - 1] = '\0';
    }
    if (json_get_str(body, "client_id", s, sizeof s) == 0) {
        strncpy(mq.client_id, s, sizeof mq.client_id - 1);
        mq.client_id[sizeof mq.client_id - 1] = '\0';
    }
    if (json_get_str(body, "base_topic", s, sizeof s) == 0) {
        strncpy(mq.base_topic, s, sizeof mq.base_topic - 1);
        mq.base_topic[sizeof mq.base_topic - 1] = '\0';
    }
    if (json_get_str(body, "home_assistant_prefix", s, sizeof s) == 0) {
        strncpy(mq.home_assistant_prefix, s, sizeof mq.home_assistant_prefix - 1);
        mq.home_assistant_prefix[sizeof mq.home_assistant_prefix - 1] = '\0';
    }

    int v = 0;
    if (json_get_int(body, "port", &v) == 0) {
        if (v < 1 || v > 65535) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "port must be 1..65535");
            return ESP_FAIL;
        }
        mq.port = (uint16_t)v;
    }
    if (json_get_int(body, "keepalive_sec", &v) == 0) {
        if (v < 15 || v > 3600) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "keepalive_sec must be 15..3600");
            return ESP_FAIL;
        }
        mq.keepalive_sec = (uint16_t)v;
    }

    if (mq.enabled && mq.host[0] == '\0') {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "enabled MQTT requires host");
        return ESP_FAIL;
    }
    if (!safe_mqtt_text(mq.host, !mq.enabled, false) ||
        !safe_profile_text(mq.username, true) ||
        !safe_profile_text(mq.password, true) ||
        !safe_mqtt_text(mq.client_id, false, false) ||
        !safe_mqtt_text(mq.base_topic, false, true) ||
        !safe_mqtt_text(mq.home_assistant_prefix, false, true)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad MQTT text");
        return ESP_FAIL;
    }

    esp_err_t err = config_store_save_mqtt(&mq);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "save failed");
        return ESP_FAIL;
    }
    err = mqtt_bridge_reconfigure();
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "reconfigure failed");
        return ESP_FAIL;
    }

    char out[160];
    snprintf(out, sizeof out,
             "{\"saved\":true,\"applied\":true,\"enabled\":%s,\"connected\":%s,\"status\":%u}",
             mq.enabled ? "true" : "false",
             mqtt_bridge_is_connected() ? "true" : "false",
             (unsigned)modbus_store_get(HYDRA_MODBUS_REG_MQTT_STATUS));
    return send_json(req, out);
}

/* ===== profiles (named channel intensity mixes) ===== */

static esp_err_t get_profiles(httpd_req_t *req)
{
    config_profiles_t p;
    if (config_store_load_profiles(&p) != ESP_OK) {
        p.count = 0;
    }
    char buf[2048];
    size_t off = 0;
    off += snprintf(buf + off, sizeof buf - off, "{\"profiles\":[");
    bool first = true;
    for (size_t i = 0; i < k_builtin_profile_count && off < sizeof buf - 350; ++i) {
        off += snprintf(buf + off, sizeof buf - off,
            "%s{\"name\":\"%s\",\"description\":\"%s\",\"builtin\":true,\"intensities\":[",
            first ? "" : ",", k_builtin_profiles[i].name,
            k_builtin_profiles[i].description);
        for (int j = 0; j < 9; j++) {
            off += snprintf(buf + off, sizeof buf - off,
                "%s%u", j==0 ? "" : ",", (unsigned)k_builtin_profiles[i].intensities[j]);
        }
        off += snprintf(buf + off, sizeof buf - off, "]}");
        first = false;
    }
    for (size_t i = 0; i < p.count && off < sizeof buf - 350; ++i) {
        if (builtin_profile_by_name(p.profiles[i].name)) continue;
        off += snprintf(buf + off, sizeof buf - off,
            "%s{\"name\":\"%s\",\"description\":\"%s\",\"builtin\":false,\"intensities\":[",
            first ? "" : ",", p.profiles[i].name,
            p.profiles[i].description);
        for (int j = 0; j < 9; j++) {
            off += snprintf(buf + off, sizeof buf - off,
                "%s%u", j==0 ? "" : ",", (unsigned)p.profiles[i].intensities[j]);
        }
        off += snprintf(buf + off, sizeof buf - off, "]}");
        first = false;
    }
    off += snprintf(buf + off, sizeof buf - off, "]}");
    return send_json(req, buf);
}

static esp_err_t post_profile(httpd_req_t *req)
{
    char body[768];
    int total = 0;
    while (total < (int)sizeof body - 1) {
        int got = httpd_req_recv(req, body + total, sizeof body - 1 - total);
        if (got <= 0) break;
        total += got;
    }
    body[total < 0 ? 0 : total] = '\0';

    config_profiles_t p;
    if (config_store_load_profiles(&p) != ESP_OK) {
        p.count = 0;
    }

    char name[CONFIG_PROFILE_NAME_LEN];
    if (json_get_str(body, "name", name, sizeof name) != 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing name");
        return ESP_FAIL;
    }
    if (!safe_profile_text(name, false)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad name");
        return ESP_FAIL;
    }
    if (builtin_profile_by_name(name)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "built-in profile name is reserved");
        return ESP_FAIL;
    }
    char description[CONFIG_PROFILE_DESC_LEN] = {0};
    if (json_get_str(body, "description", description, sizeof description) != 0) {
        description[0] = '\0';
    }
    if (!safe_profile_text(description, true)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad description");
        return ESP_FAIL;
    }

    int idx = -1;
    for (size_t i = 0; i < p.count; ++i) {
        if (strcmp(p.profiles[i].name, name) == 0) {
            idx = (int)i; break;
        }
    }
    if (idx < 0) {
        if (p.count >= MAX_USER_PROFILES) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "too many profiles");
            return ESP_FAIL;
        }
        idx = p.count;
        strncpy(p.profiles[idx].name, name, sizeof p.profiles[idx].name - 1);
        p.profiles[idx].name[sizeof p.profiles[idx].name - 1] = '\0';
        p.profiles[idx].description[0] = '\0';
        memset(p.profiles[idx].intensities, 0, sizeof p.profiles[idx].intensities);
        p.count++;
    }
    strncpy(p.profiles[idx].description, description,
            sizeof p.profiles[idx].description - 1);
    p.profiles[idx].description[sizeof p.profiles[idx].description - 1] = '\0';

    for (int j = 0; j < 9; j++) {
        int v;
        if (json_get_int(body, channel_names[j], &v) == 0) {
            if (v < 0) v = 0;
            if (v > 1000) v = 1000;
            p.profiles[idx].intensities[j] = (uint16_t)v;
        }
    }

    if (config_store_save_profiles(&p) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "save failed");
        return ESP_FAIL;
    }
    char out[320];
    snprintf(out, sizeof out, "{\"saved\":true,\"name\":\"%s\",\"description\":\"%s\"}",
             name, p.profiles[idx].description);
    return send_json(req, out);
}

static esp_err_t delete_profile(httpd_req_t *req)
{
    const char *p = strstr(req->uri, "/api/profiles/");
    if (!p) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad uri"); return ESP_FAIL; }
    p += strlen("/api/profiles/");
    char name[CONFIG_PROFILE_NAME_LEN];
    size_t i = 0;
    while (p[i] && p[i] != '/' && i + 1 < sizeof name) { name[i] = p[i]; ++i; }
    name[i] = '\0';
    if (i == 0) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing name"); return ESP_FAIL; }
    if (builtin_profile_by_name(name)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "built-in profile cannot be deleted");
        return ESP_FAIL;
    }

    config_profiles_t profs;
    if (config_store_load_profiles(&profs) != ESP_OK) profs.count = 0;

    int found = -1;
    for (size_t k = 0; k < profs.count; ++k) {
        if (strcmp(profs.profiles[k].name, name) == 0) { found = (int)k; break; }
    }
    if (found < 0) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "not found");
        return ESP_FAIL;
    }
    for (size_t k = found; k < profs.count - 1; ++k) {
        profs.profiles[k] = profs.profiles[k + 1];
    }
    profs.count--;
    if (config_store_save_profiles(&profs) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "save failed");
        return ESP_FAIL;
    }
    char out[96];
    snprintf(out, sizeof out, "{\"deleted\":true,\"name\":\"%s\"}", name);
    return send_json(req, out);
}

static esp_err_t submit_web_command(httpd_req_t *req,
                                    const char *target_id,
                                    ce_target_type_t target_type,
                                    const char *body)
{
    ce_request_t r;
    memset(&r, 0, sizeof r);
    strncpy(r.target_id, target_id, LIGHT_ID_LEN - 1);
    r.target_type = target_type;
    r.scene_timeout_sec = 60;
    r.command_timeout_ms = 30000;
    r.replace = true;

    bool special = false;
    char prof_name[CONFIG_PROFILE_NAME_LEN] = {0};
    if (json_get_str(body, "profile", prof_name, sizeof prof_name) == 0) {
        const user_profile_t *builtin = builtin_profile_by_name(prof_name);
        if (builtin) {
            r.kind = CE_KIND_SET_CHANNELS;
            r.channel_count = 9;
            for (int jj = 0; jj < 9; ++jj) {
                r.channels[jj].name = channel_names[jj];
                r.channels[jj].value = builtin->intensities[jj];
            }
            special = true;
        }
        config_profiles_t ps;
        if (!special && config_store_load_profiles(&ps) == ESP_OK) {
            for (uint8_t ii = 0; ii < ps.count; ++ii) {
                if (strcmp(ps.profiles[ii].name, prof_name) == 0) {
                    r.kind = CE_KIND_SET_CHANNELS;
                    r.channel_count = 9;
                    for (int jj = 0; jj < 9; ++jj) {
                        r.channels[jj].name = channel_names[jj];
                        r.channels[jj].value = ps.profiles[ii].intensities[jj];
                    }
                    special = true;
                    break;
                }
            }
        }
    }
    int idelta = 0;
    if (!special && json_get_int(body, "intensity_delta", &idelta) == 0) {
        r.kind = CE_KIND_INTENSITY_ADJUST;
        r.intensity_delta = (int16_t)idelta;
        special = true;
    }
    if (!special) {
        if (target_type != CE_TARGET_LIGHT || mqtt_parse_light_command(body, target_id, &r) != 0) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid command");
            return ESP_FAIL;
        }
        r.target_type = CE_TARGET_LIGHT;
    }
    r.source = CMD_SOURCE_WEB;

    char cmd_id[CMD_ID_LEN];
    ce_result_t res = command_engine_submit(&r, cmd_id);

    char out[128];
    snprintf(out, sizeof out,
             "{\"command_id\":\"%s\",\"result\":%d}", cmd_id, (int)res);
    return send_json(req, out);
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

    if (strcmp(slash, "/rename") == 0) {
        char display_name[LIGHT_NAME_LEN];
        if (json_get_str(body, "display_name", display_name, sizeof display_name) != 0) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing display_name");
            return ESP_FAIL;
        }
        if (!safe_display_name(display_name)) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad display_name");
            return ESP_FAIL;
        }
        if (light_registry_rename(light_id, display_name) != 0) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "rename failed");
            return ESP_FAIL;
        }
        light_registry_save();
        const registered_light_t *light = light_registry_get(light_id);
        char out[128];
        snprintf(out, sizeof out,
                 "{\"renamed\":true,\"light_id\":\"%s\",\"display_name\":\"%s\"}",
                 light_id, light ? light->display_name : display_name);
        return send_json(req, out);
    }

    if (strcmp(slash, "/command") != 0) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "unknown light subpath");
        return ESP_FAIL;
    }

    return submit_web_command(req, light_id, CE_TARGET_LIGHT, body);
}

static esp_err_t post_group_command(httpd_req_t *req)
{
    const char *p = strstr(req->uri, "/api/groups/");
    if (!p) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad uri"); return ESP_FAIL; }
    p += strlen("/api/groups/");
    const char *slash = strchr(p, '/');
    if (!slash) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing group_id"); return ESP_FAIL; }
    char group_id[GROUP_ID_LEN];
    size_t id_len = slash - p;
    if (id_len >= GROUP_ID_LEN) id_len = GROUP_ID_LEN - 1;
    memcpy(group_id, p, id_len);
    group_id[id_len] = '\0';
    if (strcmp(slash, "/command") != 0) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "unknown group subpath");
        return ESP_FAIL;
    }

    char body[1024];
    int total = 0;
    while (total < (int)sizeof body - 1) {
        int got = httpd_req_recv(req, body + total, sizeof body - 1 - total);
        if (got <= 0) break;
        total += got;
    }
    body[total < 0 ? 0 : total] = '\0';

    return submit_web_command(req, group_id, CE_TARGET_GROUP, body);
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
    cfg.max_uri_handlers = 28;
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
    static const httpd_uri_t r_light_del     = { .uri = "/api/lights/*",         .method = HTTP_DELETE, .handler = delete_light };
    static const httpd_uri_t r_groups_get    = { .uri = "/api/groups",           .method = HTTP_GET,  .handler = get_groups };
    static const httpd_uri_t r_groups_post   = { .uri = "/api/groups",           .method = HTTP_POST, .handler = post_groups };
    static const httpd_uri_t r_group_cmd     = { .uri = "/api/groups/*",         .method = HTTP_POST, .handler = post_group_command };
    static const httpd_uri_t r_group_del     = { .uri = "/api/groups/*",         .method = HTTP_DELETE, .handler = delete_group };
    static const httpd_uri_t r_logs          = { .uri = "/api/logs",             .method = HTTP_GET,  .handler = get_logs };
    static const httpd_uri_t r_ota           = { .uri = "/api/ota",              .method = HTTP_POST, .handler = post_ota };
    static const httpd_uri_t r_wifi_get      = { .uri = "/api/config/wifi",      .method = HTTP_GET,  .handler = get_wifi_config };
    static const httpd_uri_t r_wifi_post     = { .uri = "/api/config/wifi",      .method = HTTP_POST, .handler = post_wifi_config };
    static const httpd_uri_t r_modbus_get    = { .uri = "/api/config/modbus",    .method = HTTP_GET,  .handler = get_modbus_config };
    static const httpd_uri_t r_modbus_post   = { .uri = "/api/config/modbus",    .method = HTTP_POST, .handler = post_modbus_config };
    static const httpd_uri_t r_mqtt_get      = { .uri = "/api/config/mqtt",      .method = HTTP_GET,  .handler = get_mqtt_config };
    static const httpd_uri_t r_mqtt_post     = { .uri = "/api/config/mqtt",      .method = HTTP_POST, .handler = post_mqtt_config };
    static const httpd_uri_t r_profiles_get  = { .uri = "/api/profiles",         .method = HTTP_GET,  .handler = get_profiles };
    static const httpd_uri_t r_profiles_post = { .uri = "/api/profiles",         .method = HTTP_POST, .handler = post_profile };
    static const httpd_uri_t r_profile_del   = { .uri = "/api/profiles/*",       .method = HTTP_DELETE, .handler = delete_profile };

    httpd_register_uri_handler(s_server, &r_root);
    httpd_register_uri_handler(s_server, &r_status);
    httpd_register_uri_handler(s_server, &r_scan);
    httpd_register_uri_handler(s_server, &r_scan_results);
    httpd_register_uri_handler(s_server, &r_lights_get);
    httpd_register_uri_handler(s_server, &r_lights_post);
    httpd_register_uri_handler(s_server, &r_light_cmd);
    httpd_register_uri_handler(s_server, &r_light_del);
    httpd_register_uri_handler(s_server, &r_groups_get);
    httpd_register_uri_handler(s_server, &r_groups_post);
    httpd_register_uri_handler(s_server, &r_group_cmd);
    httpd_register_uri_handler(s_server, &r_group_del);
    httpd_register_uri_handler(s_server, &r_logs);
    httpd_register_uri_handler(s_server, &r_ota);
    httpd_register_uri_handler(s_server, &r_wifi_get);
    httpd_register_uri_handler(s_server, &r_wifi_post);
    httpd_register_uri_handler(s_server, &r_modbus_get);
    httpd_register_uri_handler(s_server, &r_modbus_post);
    httpd_register_uri_handler(s_server, &r_mqtt_get);
    httpd_register_uri_handler(s_server, &r_mqtt_post);
    httpd_register_uri_handler(s_server, &r_profiles_get);
    httpd_register_uri_handler(s_server, &r_profiles_post);
    httpd_register_uri_handler(s_server, &r_profile_del);

    return ESP_OK;
}

#endif /* ESP_PLATFORM */
