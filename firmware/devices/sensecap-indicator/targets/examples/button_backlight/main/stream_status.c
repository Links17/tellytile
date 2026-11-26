#include "stream_status.h"

#include <stdio.h>

enum { SAMPLE_US = 1000000 };

tv_link_status_t tv_status_link(bool wifi, bool streaming, bool recovering, unsigned fps_tenths)
{
    if (!wifi) return TV_LINK_WAITING;
    if (recovering) return TV_LINK_RECOVERING;
    return streaming && fps_tenths ? TV_LINK_PLAYING : TV_LINK_CONNECTING;
}

tv_audio_status_t tv_status_audio(bool configured, uint32_t session, int64_t now_us, int64_t synced_us)
{
    if (!configured) return TV_AUDIO_DISABLED;
    if (session && synced_us > 0 && now_us >= synced_us && now_us - synced_us <= 3 * SAMPLE_US)
        return TV_AUDIO_SYNCED;
    return TV_AUDIO_WAITING;
}

unsigned tv_wifi_bars(int rssi)
{
    if (rssi >= 0) return 0;
    if (rssi >= -55) return 4;
    if (rssi >= -65) return 3;
    if (rssi >= -75) return 2;
    return 1;
}

void tv_status_reset(tv_status_t *status, int64_t now_us, tv_stream_totals_t totals)
{
    status->sampled_us = now_us;
    status->previous = totals;
    status->reading.fps_tenths = 0;
    status->reading.kib_per_second = 0;
}

bool tv_status_sample(tv_status_t *status, int64_t now_us, tv_stream_totals_t totals,
                      bool streaming)
{
    if (now_us - status->sampled_us < SAMPLE_US) return false;
    const uint64_t elapsed = now_us - status->sampled_us;
    /* Unsigned subtraction also accounts for wrap of the cumulative counters. */
    const uint32_t frames = totals.frames - status->previous.frames;
    const uint32_t bytes = totals.bytes - status->previous.bytes;
    tv_status_reset(status, now_us, totals);
    if (streaming) {
        const uint64_t fps = ((uint64_t)frames * 10 * SAMPLE_US + elapsed / 2) / elapsed;
        const uint64_t kib = (((uint64_t)bytes * SAMPLE_US + elapsed / 2) / elapsed + 512) / 1024;
        status->reading.fps_tenths = fps > 999 ? 999 : (unsigned)fps;
        status->reading.kib_per_second = kib > 9999 ? 9999 : (unsigned)kib;
    }
    return true;
}

void tv_status_draw(ui_surface_t surface, tv_status_reading_t reading)
{
    const uint16_t background = 0x0842;
    const int y = TV_STATUS_Y + (TV_STATUS_ROWS - UI_TEXT_LINE_H) / 2;
    char field[28];
    ui_text_fill(surface, 0, TV_STATUS_Y, surface.width, TV_STATUS_ROWS, background);
    if (reading.wifi_bars) snprintf(field, sizeof field, "Wi-Fi %u/4", reading.wifi_bars);
    else snprintf(field, sizeof field, "Wi-Fi --");
    ui_text_draw(surface, 16, y, field, UI_COLOR_TEXT, background);
    snprintf(field, sizeof field, "FPS %u.%u", reading.fps_tenths / 10, reading.fps_tenths % 10);
    ui_text_draw(surface, 129, y, field, UI_COLOR_TEXT, background);
    snprintf(field, sizeof field, "RX %u KiB/s", reading.kib_per_second);
    ui_text_draw(surface, 252, y, field, UI_COLOR_TEXT, background);
    ui_text_draw(surface, surface.width - 16 - ui_text_width("USB 5V"), y, "USB 5V",
                 UI_COLOR_TEXT, background);
}
