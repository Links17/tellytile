#pragma once

#include "esp_err.h"
#include "tv_config.h"

/* The palette may arrive while the transcoder is still buffering media. */
unsigned tv_header_timeout_ms(bool configured, bool media_seen);

/* Called before each media socket. A failed refresh cannot authorize playback. */
esp_err_t tv_prepare_session(const char *origin, const char *code, tv_connection_t *connection);
