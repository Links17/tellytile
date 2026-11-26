#include "setup_portal.h"

#include "av_protocol.h"
#include "cJSON.h"

#ifdef ESP_PLATFORM
#include "lwip/inet.h"
#include "lwip/sockets.h"
#else
#include <arpa/inet.h>
#endif
#include <string.h>

static bool text_valid(const char *text, size_t capacity)
{
    const char *end = memchr(text, 0, capacity);
    if (!end) return false;
    for (; text != end; ++text) {
        if ((unsigned char)*text < 32 || *text == 127) return false;
    }
    return true;
}

bool portal_config_valid(const portal_config_t *c)
{
    if (!c->tv.ssid[0] || !text_valid(c->tv.ssid, sizeof c->tv.ssid)
        || !text_valid(c->tv.host, sizeof c->tv.host)
        || !text_valid(c->tv.password, sizeof c->tv.password)
        || !text_valid(c->tv.token, sizeof c->tv.token)
        || !text_valid(c->origin, sizeof c->origin) || !text_valid(c->code, sizeof c->code)
        || !text_valid(c->audio_peer, sizeof c->audio_peer) || !c->tv.port || !c->audio_port)
        return false;
    size_t password_size = strlen(c->tv.password);
    if (password_size && (password_size < 8 || (password_size == 64
        && strspn(c->tv.password, "0123456789abcdefABCDEF") != 64))) return false;
    if (c->audio_peer[0]) {
        struct in_addr address;
        if (inet_pton(AF_INET, c->audio_peer, &address) != 1) return false;
    }
    if (c->origin[0]) {
        char normalized[TV_ORIGIN_CAP];
        return tv_origin_from_input(c->origin, c->tv.port, NULL, normalized)
            && !strcmp(normalized, c->origin)
            && (!c->code[0] || (strlen(c->code) == 4
                && strspn(c->code, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789") == 4));
    }
    return tv_host_valid(c->tv.host, sizeof c->tv.host);
}

static bool parse_str(cJSON *o, const char *key_name, char *out, size_t cap)
{
    const char *in = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(o, key_name));
    return in && strlcpy(out, in, cap) < cap;
}

static bool parse_secret(cJSON *json, const char *key, char *out, size_t cap, const char *saved)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(json, key);
    if (cJSON_IsNull(item)) return saved && strlcpy(out, saved, cap) < cap;
    return parse_str(json, key, out, cap);
}

static bool parse_port(cJSON *o, const char *key_name, uint16_t *out)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(o, key_name);
    if (!cJSON_IsNumber(item) || item->valuedouble != item->valueint ||
        item->valueint < 1 || item->valueint > 65535) return false;
    *out = (uint16_t)item->valueint;
    return true;
}

bool portal_config_parse(const char *body, size_t length, const portal_config_t *saved, portal_config_t *out)
{
    if (memchr(body, 0, length) || !av_json_depth_safe(body, length, 2) || strstr(body, "\\u0000")) return false;
    cJSON *json = cJSON_ParseWithLengthOpts(body, length + 1, NULL, true);
    char origin_input[TV_ORIGIN_CAP];
    bool ok = cJSON_IsObject(json) &&
        parse_str(json, "ssid", out->tv.ssid, sizeof out->tv.ssid) &&
        parse_str(json, "host", out->tv.host, sizeof out->tv.host) &&
        parse_str(json, "origin", origin_input, sizeof origin_input) &&
        parse_str(json, "code", out->code, sizeof out->code) &&
        parse_str(json, "audio_peer", out->audio_peer, sizeof out->audio_peer) &&
        parse_port(json, "port", &out->tv.port) &&
        parse_port(json, "audio_port", &out->audio_port);
    if (ok) {
        out->origin[0] = 0;
        if (origin_input[0])
            ok = tv_origin_from_input(origin_input, out->tv.port, NULL, out->origin);
    }
    if (ok) {
        const bool keep_password = saved && !strcmp(saved->tv.ssid, out->tv.ssid);
        const bool keep_token = saved && !saved->origin[0] && !out->origin[0] &&
                                saved->tv.port == out->tv.port && !strcmp(saved->tv.host, out->tv.host);
        ok = parse_secret(json, "password", out->tv.password, sizeof out->tv.password,
                          keep_password ? saved->tv.password : NULL) &&
             parse_secret(json, "token", out->tv.token, sizeof out->tv.token,
                          keep_token ? saved->tv.token : NULL);
    }
    cJSON_Delete(json);
    return ok && portal_config_valid(out);
}

char *portal_config_public_json(const portal_config_t *saved)
{
    cJSON *doc = cJSON_CreateObject();
    if (!doc) return NULL;
    bool ok = cJSON_AddBoolToObject(doc, "saved", saved != NULL) != NULL;
    if (ok && saved)
        ok = cJSON_AddStringToObject(doc, "ssid", saved->tv.ssid) &&
             cJSON_AddStringToObject(doc, "host", saved->tv.host) &&
             cJSON_AddNumberToObject(doc, "port", saved->tv.port) &&
             cJSON_AddStringToObject(doc, "origin", saved->origin) &&
             cJSON_AddStringToObject(doc, "audio_peer", saved->audio_peer) &&
             cJSON_AddNumberToObject(doc, "audio_port", saved->audio_port) &&
             cJSON_AddBoolToObject(doc, "has_password", saved->tv.password[0] != 0) &&
             cJSON_AddBoolToObject(doc, "has_token", !saved->origin[0] && saved->tv.token[0] != 0);
    char *result = ok ? cJSON_PrintUnformatted(doc) : NULL;
    cJSON_Delete(doc);
    return result;
}

static bool ssid_utf8(const char *text, size_t capacity)
{
    if (!text[0] || !text_valid(text, capacity)) return false;
    const unsigned char *p = (const unsigned char *)text;
    while (*p) {
        const unsigned lead = *p++;
        if (lead < 0x80) continue;
        if (lead < 0xc2 || lead > 0xf4) return false;
        unsigned tail = lead < 0xe0 ? 1 : lead < 0xf0 ? 2 : 3;
        if ((lead == 0xe0 && *p < 0xa0) || (lead == 0xed && *p >= 0xa0) ||
            (lead == 0xf0 && *p < 0x90) || (lead == 0xf4 && *p >= 0x90)) return false;
        while (tail--) {
            if ((*p & 0xc0) != 0x80) return false;
            ++p;
        }
    }
    return true;
}

char *portal_scan_json(const portal_network_t *networks, size_t count)
{
    cJSON *rows = cJSON_CreateArray();
    if (!rows) return NULL;
    bool ok = true;
    size_t used = 0;
    for (size_t i = 0; i < count && used < PORTAL_SCAN_RESULTS && ok; ++i) {
        const portal_network_t *network = &networks[i];
        if (!ssid_utf8(network->ssid, sizeof network->ssid)) continue;
        bool duplicate = false;
        const cJSON *row;
        cJSON_ArrayForEach(row, rows) {
            const char *shown = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(row, "ssid"));
            if (!strcmp(network->ssid, shown)) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) continue; /* Scan records arrive in signal-strength order. */
        cJSON *entry = cJSON_CreateObject();
        ok = entry && cJSON_AddStringToObject(entry, "ssid", network->ssid) &&
             cJSON_AddNumberToObject(entry, "rssi", network->rssi) &&
             cJSON_AddBoolToObject(entry, "open", network->open) && cJSON_AddItemToArray(rows, entry);
        if (!ok) cJSON_Delete(entry);
        ++used;
    }
    char *result = ok ? cJSON_PrintUnformatted(rows) : NULL;
    cJSON_Delete(rows);
    return result;
}
