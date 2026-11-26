#include "session_connection.h"
#include "av_protocol.h"
#include "pairing_client.h"

#include <string.h>

unsigned tv_header_timeout_ms(bool configured, bool media_seen)
{
    if (!configured) return 2000;
    return media_seen ? 5000 : AV_FIRST_MEDIA_TIMEOUT_MS;
}

esp_err_t tv_prepare_session(const char *origin, const char *code, tv_connection_t *connection)
{
    if (!origin[0]) return ESP_OK;
    tv_connection_t next = *connection;
    esp_err_t result = pairing_client_refresh(origin, &next, 6000);
    if (result == ESP_ERR_NOT_FOUND && code[0]) {
        bool pending = false;
        result = pairing_client_pair(origin, code, &next, &pending);
        if (result == ESP_OK && pending) result = pairing_client_committed(origin);
    }
    if (result == ESP_OK) *connection = next;
    memset(&next, 0, sizeof next);
    return result;
}
