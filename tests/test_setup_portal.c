#include "setup_portal.h"
#include "cJSON.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static bool parse(const char *ssid, const char *host, unsigned port, const char *origin,
                  const char *password, const char *token, const portal_config_t *saved,
                  portal_config_t *out)
{
    char json[1024];
    const int length = snprintf(json, sizeof json,
        "{\"ssid\":\"%s\",\"host\":\"%s\",\"port\":%u,\"origin\":\"%s\","
        "\"password\":%s,\"token\":%s,\"code\":\"\",\"audio_peer\":\"192.168.1.2\","
        "\"audio_port\":18096}", ssid, host, port, origin, password, token);
    assert(length > 0 && length < (int)sizeof json);
    return portal_config_parse(json, (size_t)length, saved, out);
}

static void check_scan(void)
{
    const portal_network_t networks[] = {
        {"大屏 \"studio\" <script>", -40, false},
        {"", -42, false},
        {"大屏 \"studio\" <script>", -44, true},
        {"Guest", -52, true},
        {"broken\xf0\x9f", -55, false},
        {"control\nname", -62, false},
        {"12345678901234567890123456789012", -70, false},
    };
    char *json = portal_scan_json(networks, sizeof networks / sizeof networks[0]);
    assert(json);
    cJSON *rows = cJSON_Parse(json);
    assert(cJSON_GetArraySize(rows) == 3);
    const cJSON *first = cJSON_GetArrayItem(rows, 0);
    assert(!strcmp(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(first, "ssid")), networks[0].ssid));
    assert(cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(first, "rssi")) == -40);
    assert(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(first, "open")));
    const cJSON *guest = cJSON_GetArrayItem(rows, 1);
    assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(guest, "open")));
    assert(!strcmp(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(rows, 2), "ssid")),
                   networks[6].ssid));
    cJSON_Delete(rows);
    cJSON_free(json);
    json = portal_scan_json(NULL, 0);
    assert(json && !strcmp(json, "[]"));
    cJSON_free(json);
    puts("Scan results retained the strongest SSID, 32-byte names and quoted UTF-8, skipping empty and invalid names.");
}

int main(void)
{
    check_scan();
    portal_config_t saved = {0}, change = {0};
    assert(parse("studio", "tv.local", 10745, "", "\"my-password\"", "\"my-token\"", NULL, &saved));
    assert(parse("studio", "tv.local", 10745, "", "null", "null", &saved, &change));
    assert(!strcmp(change.tv.password, saved.tv.password) && !strcmp(change.tv.token, saved.tv.token));
    assert(!strcmp(change.audio_peer, "192.168.1.2") && change.audio_port == 18096);
    assert(!parse("guest", "tv.local", 10745, "", "null", "null", &saved, &change));
    assert(!parse("studio", "other.local", 10745, "", "null", "null", &saved, &change));
    assert(!parse("studio", "tv.local", 10746, "", "null", "null", &saved, &change));
    assert(!parse("studio", "tv.local", 10745, "http://tv.local:10745", "null", "null", &saved, &change));
    assert(!parse("studio", "tv.local", 10745, "", "null", "null", NULL, &change));
    assert(parse("guest", "other.local", 10746, "", "\"\"", "\"\"", &saved, &change));
    assert(!change.tv.password[0] && !change.tv.token[0]);
    assert(parse("studio", "", 10745, "http://tv.local:10745", "null", "\"\"", &saved, &change));
    assert(!strcmp(change.origin, "http://tv.local:10745") && !change.code[0]);
    memset(change.tv.host, 'A', sizeof change.tv.host);
    assert(!portal_config_valid(&change));

    strcpy(saved.tv.ssid, "office \"大屏\" <script>");
    char *public = portal_config_public_json(&saved);
    assert(public && !strstr(public, "my-password") && !strstr(public, "my-token"));
    cJSON *doc = cJSON_Parse(public);
    assert(doc && cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(doc, "saved")));
    assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(doc, "has_password")));
    assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(doc, "has_token")));
    assert(!strcmp(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(doc, "ssid")), saved.tv.ssid));
    assert(!cJSON_GetObjectItemCaseSensitive(doc, "password"));
    assert(!cJSON_GetObjectItemCaseSensitive(doc, "token"));
    assert(!cJSON_GetObjectItemCaseSensitive(doc, "code"));
    cJSON_Delete(doc);
    cJSON_free(public);
    public = portal_config_public_json(NULL);
    assert(public && !strcmp(public, "{\"saved\":false}"));
    cJSON_free(public);
    puts("Setup edits kept matched credentials, rejected stale identities, accepted explicit clearing and excluded secrets from saved metadata.");
}
