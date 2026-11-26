#include "session_connection.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned refreshed, paired, committed;
static esp_err_t refresh_result, commit_result;

esp_err_t pairing_client_refresh(const char *origin, tv_connection_t *next, uint32_t timeout_ms)
{
    assert(!strcmp(origin, "http://issuer.local") && timeout_ms);
    ++refreshed;
    if (refresh_result == ESP_OK) {
        strcpy(next->host, refreshed == 1 ? "media-a.local" : "media-b.local");
        strcpy(next->token, refreshed == 1 ? "initial-ticket" : "rotated-ticket");
        next->port = refreshed == 1 ? 10745 : 10845;
    }
    return refresh_result;
}

esp_err_t pairing_client_pair(const char *origin, const char *code, tv_connection_t *next, bool *pending)
{
    assert(!strcmp(origin, "http://issuer.local") && !strcmp(code, "ABCD"));
    ++paired;
    strcpy(next->token, "enrolled-ticket");
    *pending = true;
    return ESP_OK;
}

esp_err_t pairing_client_committed(const char *origin)
{
    assert(!strcmp(origin, "http://issuer.local"));
    ++committed;
    return commit_result;
}

int main(void)
{
    assert(tv_header_timeout_ms(false, false) == 2000);
    assert(tv_header_timeout_ms(true, false) == 30000);
    assert(tv_header_timeout_ms(true, true) == 5000);
    assert(tv_header_timeout_ms(false, false) == 2000); /* New session. */
    puts("Receive deadlines follow CONFIG, first valid media, and active playback independently of the palette.");
    const char *issuer = "http://issuer.local";
    tv_connection_t connection = {.ssid = "studio", .host = "manual.local", .token = "manual-ticket"};
    assert(tv_prepare_session("", "", &connection) == ESP_OK);
    assert(refreshed == 0 && !strcmp(connection.token, "manual-ticket"));

    assert(tv_prepare_session(issuer, "", &connection) == ESP_OK);
    assert(refreshed == 1 && !strcmp(connection.host, "media-a.local") && connection.port == 10745);
    assert(!strcmp(connection.token, "initial-ticket"));
    assert(tv_prepare_session(issuer, "", &connection) == ESP_OK);
    assert(refreshed == 2 && !strcmp(connection.host, "media-b.local") && connection.port == 10845);
    assert(!strcmp(connection.token, "rotated-ticket") && !strcmp(connection.ssid, "studio"));

    refresh_result = ESP_ERR_TIMEOUT;
    assert(tv_prepare_session(issuer, "ABCD", &connection) == ESP_ERR_TIMEOUT && paired == 0);
    refresh_result = ESP_ERR_NOT_FOUND;
    assert(tv_prepare_session(issuer, "", &connection) == ESP_ERR_NOT_FOUND && paired == 0);
    commit_result = ESP_FAIL;
    assert(tv_prepare_session(issuer, "ABCD", &connection) == ESP_FAIL && paired == 1 && committed == 1);
    assert(!strcmp(connection.token, "rotated-ticket"));
    commit_result = ESP_OK;
    assert(tv_prepare_session(issuer, "ABCD", &connection) == ESP_OK && paired == 2 && committed == 2);
    assert(!strcmp(connection.token, "enrolled-ticket"));
    puts("Session setup refreshed each managed connection, replaced the endpoint and ticket together, and waited for durable enrollment.");
}
