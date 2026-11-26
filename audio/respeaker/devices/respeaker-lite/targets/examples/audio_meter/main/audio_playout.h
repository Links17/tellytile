#pragma once

#include "relay_audio.h"

/* The pinned server's 320 ms lead and 280 ms lookahead can arrive in a burst. */
enum { AUDIO_WINDOW = 32, AUDIO_RETIRED = 8 };

typedef struct {
    bool valid;
    uint32_t pts;
    uint8_t block[AV_AUDIO_BYTES];
} audio_slot_t;

typedef struct {
    uint32_t session, expected, end_pts;
    uint32_t retired[AUDIO_RETIRED];
    unsigned retire_next;
    bool started;
    int64_t deadline_us, last_packet_us;
    audio_slot_t slots[AUDIO_WINDOW];
} audio_playout_t;

typedef enum { AUDIO_WAIT, AUDIO_BLOCK, AUDIO_SILENCE } audio_due_t;

bool audio_playout_receive(audio_playout_t *state, const uint8_t *wire, size_t size, int64_t now_us);
bool audio_playout_timeline(const audio_playout_t *state, uint32_t session, int64_t now_us,
                            uint32_t *pts_ms, uint32_t *delay_us);
audio_due_t audio_playout_take(audio_playout_t *state, int64_t now_us, uint8_t block[AV_AUDIO_BYTES]);
