#include "setup_portal.h"

#include "cJSON.h"
#include "bootloader_random.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "nvs.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { BODY_LIMIT = 1536, SCAN_RECORD_LIMIT = 48 };
static const char *TAG = "passport_tv_setup";
static const char key[] = "settings_v1";
static char csrf[17];
static TaskHandle_t setup_task;
static bool setup_finished;

static const char page[] =
    "<html lang=en><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>TV receiver setup</title><style>"
    "body{font:1rem system-ui;margin:2rem auto;max-width:30rem;padding:1rem}"
    "label{display:block;margin:1rem 0}input{display:block;width:100%;font:inherit}"
    "</style><main><h1>TV receiver setup</h1><form>"
    "<label>Wi-Fi name<input name=ssid maxlength=32 required></label>"
    "<button type=button id=scan disabled>Scan Wi-Fi</button> "
    "<label id=networkLabel hidden>Nearby 2.4 GHz networks<select id=networks></select></label>"
    "<div id=scanStatus role=status></div>"
    "<label>Wi-Fi password<input type=password name=password maxlength=64></label>"
    "<div id=wifiKeep hidden><label><input type=checkbox name=keep_password checked>Keep saved Wi-Fi password</label></div>"
    "<label>Original service host<input name=host maxlength=63></label>"
    "<label>Service port<input name=port type=number min=1 max=65535 value=10745 required></label>"
    "<label>Pairing origin, e.g. http://my-tv.local:10745<input name=origin maxlength=128></label>"
    "<label>Four-character pairing code<input name=code maxlength=4></label>"
    "<label>Manual service token (alternative to pairing)<input type=password name=token maxlength=127></label>"
    "<div id=tokenKeep hidden><label><input type=checkbox name=keep_token checked>Keep saved manual token</label></div>"
    "<label>Speaker board IPv4 address (optional)<input name=audio_peer maxlength=15></label>"
    "<label>Speaker port<input name=audio_port type=number min=1 max=65535 value=18096 required></label>"
    "<button type=submit disabled>Save and restart</button> "
    "<button type=button id=cancel hidden>Restart without changes</button><p role=status></p></form></main>"
    "<style>input[type=checkbox]{display:inline;width:auto;margin-right:.5rem}"
    "label[hidden]{display:none}select{display:block;max-width:100%;font:inherit}</style>"
    "<script>const f=document.querySelector('form'),out=f.querySelector('p');"
    "const fields=f.elements,saveButton=f.querySelector('[type=submit]'),cancel=fields.cancel;"
    "const wifiKeep=document.querySelector('#wifiKeep'),tokenKeep=document.querySelector('#tokenKeep');"
    "const scan=document.querySelector('#scan'),networkLabel=document.querySelector('#networkLabel');"
    "const networks=document.querySelector('#networks'),scanStatus=document.querySelector('#scanStatus');"
    "const headers={'Content-Type':'application/json','X-Setup-Token':setupToken};let saved,busy=false;"
    "function updateKeeps(){"
    "wifiKeep.hidden=!(saved?.has_password&&fields.ssid.value===saved.ssid);"
    "tokenKeep.hidden=!(saved?.has_token&&!fields.origin.value&&fields.host.value===saved.host&&"
    "Number(fields.port.value)===saved.port);}"
    "f.addEventListener('input',updateKeeps);"
    "async function load(){"
    "try{const r=await fetch('/saved',{headers,cache:'no-store'});if(!r.ok)throw Error();"
    "saved=await r.json();if(saved.saved){"
    "for(const name of ['ssid','host','port','origin','audio_peer','audio_port'])fields[name].value=saved[name];"
    "cancel.hidden=false;}updateKeeps();saveButton.disabled=scan.disabled=false;"
    "}catch{out.textContent='Could not read settings. Reload this page and try again.';}}"
    "scan.onclick=async()=>{"
    "scan.disabled=true;networkLabel.hidden=true;scanStatus.textContent='Scanning nearby networks…';"
    "try{const r=await fetch('/scan',{headers,cache:'no-store'});if(!r.ok)throw Error();"
    "const rows=await r.json();"
    "networks.replaceChildren(new Option('Choose a network',''),...rows.map(n=>"
    "new Option(n.ssid+' · '+n.rssi+' dBm · '+(n.open?'Open':'Secured'),n.ssid)));"
    "networkLabel.hidden=!rows.length;"
    "scanStatus.textContent=rows.length?'':'No visible networks found. Enter the Wi-Fi name manually.';"
    "}catch{scanStatus.textContent='Could not scan. Retry or enter the Wi-Fi name manually.';}"
    "finally{scan.disabled=false;}};"
    "networks.onchange=()=>{if(networks.value&&networks.value!==fields.ssid.value){"
    "fields.ssid.value=networks.value;fields.password.value='';updateKeeps();}};"
    "async function submit(path,data){if(busy)return;busy=true;saveButton.disabled=cancel.disabled=true;"
    "try{const r=await fetch(path,{method:'POST',headers,body:JSON.stringify(data)});"
    "out.textContent=await r.text();if(r.ok)return;"
    "}catch{out.textContent='Could not reach the device.';}"
    "busy=false;saveButton.disabled=cancel.disabled=false;}"
    "f.onsubmit=e=>{e.preventDefault();const o=Object.fromEntries(new FormData(f));"
    "o.port=Number(o.port);o.audio_port=Number(o.audio_port);"
    "if(!o.password&&!wifiKeep.hidden&&fields.keep_password.checked)o.password=null;"
    "if(!o.token&&!tokenKeep.hidden&&fields.keep_token.checked)o.token=null;"
    "delete o.keep_password;delete o.keep_token;submit('/configure',o);};"
    "cancel.onclick=()=>submit('/cancel',{});load();</script></html>";

bool portal_config_read(portal_config_t *config)
{
    nvs_handle_t handle;
    if (nvs_open("ptv_setup", NVS_READONLY, &handle) != ESP_OK) return false;
    size_t size = sizeof *config;
    esp_err_t err = nvs_get_blob(handle, key, config, &size);
    nvs_close(handle);
    return err == ESP_OK && size == sizeof *config && portal_config_valid(config);
}

static bool save(const portal_config_t *config)
{
    nvs_handle_t handle;
    if (nvs_open("ptv_setup", NVS_READWRITE, &handle) != ESP_OK) return false;
    esp_err_t err = nvs_set_blob(handle, key, config, sizeof *config);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err == ESP_OK;
}

bool portal_consume_pairing_code(const char *origin, const char *code)
{
    portal_config_t saved;
    if (!portal_config_read(&saved) || strcmp(origin, saved.origin) || strcmp(code, saved.code))
        return true;
    memset(saved.code, 0, sizeof saved.code);
    return save(&saved);
}

static esp_err_t index_page(httpd_req_t *req)
{
    char prefix[96];
    snprintf(prefix, sizeof prefix, "<!doctype html><script>const setupToken='%s';</script>", csrf);
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    if (httpd_resp_sendstr_chunk(req, prefix) != ESP_OK ||
        httpd_resp_sendstr_chunk(req, page) != ESP_OK) return ESP_FAIL;
    return httpd_resp_sendstr_chunk(req, NULL);
}

static bool authorized(httpd_req_t *req)
{
    char token[sizeof csrf];
    return httpd_req_get_hdr_value_str(req, "X-Setup-Token", token, sizeof token) == ESP_OK &&
           strcmp(token, csrf) == 0;
}

static esp_err_t forbidden(httpd_req_t *req)
{
    return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Open the setup page on the device.");
}

static esp_err_t saved_settings(httpd_req_t *req)
{
    if (!authorized(req)) return forbidden(req);
    portal_config_t saved;
    char *json = portal_config_public_json(portal_config_read(&saved) ? &saved : NULL);
    if (!json) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not load settings.");
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    const esp_err_t status = httpd_resp_sendstr(req, json);
    cJSON_free(json);
    return status;
}

static esp_err_t scan_networks(httpd_req_t *req)
{
    if (!authorized(req)) return forbidden(req);
    if (setup_finished)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "The receiver is restarting.");
    wifi_ap_record_t *records = calloc(SCAN_RECORD_LIMIT, sizeof *records);
    if (!records)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not allocate scan results.");
    const wifi_scan_config_t scan = {
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active = {.min = 40, .max = 100},
        .home_chan_dwell_time = 35,
    };
    esp_err_t result = esp_wifi_scan_start(&scan, true);
    uint16_t count = SCAN_RECORD_LIMIT;
    if (result == ESP_OK) result = esp_wifi_scan_get_ap_records(&count, records);
    if (result != ESP_OK) {
        esp_wifi_clear_ap_list();
        free(records);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Wi-Fi scan failed.");
    }

    portal_network_t networks[SCAN_RECORD_LIMIT] = {0};
    for (uint16_t i = 0; i < count; ++i) {
        memcpy(networks[i].ssid, records[i].ssid, sizeof networks[i].ssid - 1);
        networks[i].rssi = records[i].rssi;
        networks[i].open = records[i].authmode == WIFI_AUTH_OPEN;
    }
    free(records);
    char *json = portal_scan_json(networks, count);
    if (!json)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not format scan results.");
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    const esp_err_t sent = httpd_resp_sendstr(req, json);
    cJSON_free(json);
    return sent;
}

static esp_err_t finish(httpd_req_t *req, const char *message)
{
    setup_finished = true;
    const esp_err_t status = httpd_resp_sendstr(req, message);
    xTaskNotifyGive(setup_task);
    return status;
}

static esp_err_t cancel_setup(httpd_req_t *req)
{
    if (!authorized(req)) return forbidden(req);
    portal_config_t saved;
    if (setup_finished || !portal_config_read(&saved))
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No saved setup to resume.");
    return finish(req, "The receiver is restarting with its saved settings.");
}

static esp_err_t configure(httpd_req_t *req)
{
    char content_type[32];
    if (!authorized(req) ||
        httpd_req_get_hdr_value_str(req, "Content-Type", content_type, sizeof content_type) != ESP_OK ||
        strcmp(content_type, "application/json")) return forbidden(req);
    if (setup_finished)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "The receiver is restarting.");
    if (req->content_len < 2 || req->content_len > BODY_LIMIT)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid settings size.");

    char body[BODY_LIMIT + 1];
    size_t used = 0;
    while (used < req->content_len) {
        int got = httpd_req_recv(req, body + used, req->content_len - used);
        if (got <= 0) return ESP_FAIL;
        used += (size_t)got;
    }
    body[used] = 0;
    portal_config_t settings = {0}, saved;
    if (!portal_config_parse(body, used, portal_config_read(&saved) ? &saved : NULL, &settings))
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Check Wi-Fi, service and speaker settings.");
    if (!save(&settings))
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Device could not save settings.");
    return finish(req, "Saved. The receiver is restarting.");
}

void portal_serve(void (*display)(const char *network))
{
    setup_task = xTaskGetCurrentTaskHandle();
    uint32_t random[3];
    /* Boot-time provisioning runs before RF or any application ADC client. */
    bootloader_random_enable();
    esp_fill_random(random, sizeof random);
    bootloader_random_disable();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    configASSERT(esp_netif_create_default_wifi_ap());
    configASSERT(esp_netif_create_default_wifi_sta());
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    wifi_config_t wifi = {0};
    snprintf((char *)wifi.ap.ssid, sizeof wifi.ap.ssid, "TV-SETUP-%04" PRIX32, random[0] & 0xffff);
    wifi.ap.ssid_len = strlen((const char *)wifi.ap.ssid);
    snprintf(csrf, sizeof csrf, "%08" PRIx32 "%08" PRIx32, random[1], random[2]);
    wifi.ap.authmode = WIFI_AUTH_OPEN;
    wifi.ap.max_connection = 2;
    wifi.ap.channel = 1;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "Open setup Wi-Fi is available at http://192.168.4.1");
    display((const char *)wifi.ap.ssid);
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;
    config.recv_wait_timeout = 3;
    httpd_handle_t http;
    ESP_ERROR_CHECK(httpd_start(&http, &config));
    const httpd_uri_t page_uri = {.uri = "/", .method = HTTP_GET, .handler = index_page};
    const httpd_uri_t config_uri = {.uri = "/configure", .method = HTTP_POST, .handler = configure};
    const httpd_uri_t saved_uri = {.uri = "/saved", .method = HTTP_GET, .handler = saved_settings};
    const httpd_uri_t scan_uri = {.uri = "/scan", .method = HTTP_GET, .handler = scan_networks};
    const httpd_uri_t cancel_uri = {.uri = "/cancel", .method = HTTP_POST, .handler = cancel_setup};
    ESP_ERROR_CHECK(httpd_register_uri_handler(http, &page_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(http, &saved_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(http, &scan_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(http, &cancel_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(http, &config_uri));
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    vTaskDelay(pdMS_TO_TICKS(250));
    esp_restart();
}
