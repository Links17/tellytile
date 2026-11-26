#pragma once

#include <stdint.h>

#include "ui_text.h"

enum { TV_STATUS_Y = 82, TV_STATUS_ROWS = 42 };

typedef struct {
    uint32_t frames, bytes;
} tv_stream_totals_t;

typedef struct {
    unsigned wifi_bars;
    int wifi_rssi;
    unsigned fps_tenths;
    unsigned kib_per_second;
} tv_status_reading_t;

typedef struct {
    int64_t sampled_us;
    tv_stream_totals_t previous;
    tv_status_reading_t reading;
} tv_status_t;

typedef enum { TV_LINK_WAITING, TV_LINK_CONNECTING, TV_LINK_PLAYING, TV_LINK_RECOVERING } tv_link_status_t;
typedef enum { TV_AUDIO_DISABLED, TV_AUDIO_WAITING, TV_AUDIO_SYNCED } tv_audio_status_t;

tv_link_status_t tv_status_link(bool wifi, bool streaming, bool recovering, unsigned fps_tenths);
tv_audio_status_t tv_status_audio(bool configured, uint32_t session, int64_t now_us, int64_t synced_us);
unsigned tv_wifi_bars(int rssi);
void tv_status_reset(tv_status_t *status, int64_t now_us, tv_stream_totals_t totals);
bool tv_status_sample(tv_status_t *status, int64_t now_us, tv_stream_totals_t totals,
                      bool streaming);
void tv_status_draw(ui_surface_t surface, tv_status_reading_t reading);
