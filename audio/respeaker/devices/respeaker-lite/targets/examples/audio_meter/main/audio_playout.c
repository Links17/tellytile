#include "audio_playout.h"

#include <string.h>

enum {
    BLOCK_US = AV_AUDIO_MS * 1000,
    START_US = 2 * BLOCK_US,
    IDLE_US = 1000 * AUDIO_SILENCE_MAX_MS,
    WINDOW_MS = AUDIO_WINDOW * AV_AUDIO_MS,
};

static bool retired(const audio_playout_t *state, uint32_t session)
{
    for (unsigned i = 0; i < AUDIO_RETIRED; ++i)
        if (state->retired[i] == session) return true;
    return false;
}

static void retire(audio_playout_t *state, uint32_t session)
{
    if (!session || retired(state, session)) return;
    state->retired[state->retire_next] = session;
    state->retire_next = (state->retire_next + 1) % AUDIO_RETIRED;
}

static void anchor(audio_playout_t *state, uint32_t pts, int64_t now_us)
{
    memset(state->slots, 0, sizeof state->slots);
    state->started = false;
    state->expected = pts;
    state->deadline_us = now_us + START_US;
    state->last_packet_us = now_us;
}

static void finish(audio_playout_t *state)
{
    retire(state, state->session);
    state->session = 0;
    state->end_pts = 0;
}

bool audio_playout_receive(audio_playout_t *state, const uint8_t *wire, size_t size, int64_t now_us)
{
    av_header_t h;
    if (size == AV_HEADER_BYTES && av_header_decode(wire, &h) && h.type == AV_END && h.session) {
        if (h.pts_ms % AV_AUDIO_MS) return false;
        if (state->session != h.session) {
            retire(state, h.session);
        } else if (h.pts_ms > state->expected && h.pts_ms - state->expected <= WINDOW_MS) {
            state->end_pts = h.pts_ms;
        } else {
            finish(state);
        }
        return true;
    }
    if (!relay_audio_header(wire, size, &h) || retired(state, h.session)) return false;
    if (state->session != h.session) {
        retire(state, state->session);
        state->session = h.session;
        state->end_pts = 0;
        anchor(state, h.pts_ms, now_us);
    } else if (!state->deadline_us || now_us - state->last_packet_us >= IDLE_US ||
               (h.pts_ms > state->expected && h.pts_ms - state->expected >= WINDOW_MS)) {
        if (h.pts_ms < state->expected) return false;
        anchor(state, h.pts_ms, now_us);
    }
    if (state->end_pts && h.pts_ms >= state->end_pts) return false;
    if (h.pts_ms < state->expected) {
        if (state->started || state->expected - h.pts_ms >= WINDOW_MS) return false;
        state->expected = h.pts_ms;
    }
    audio_slot_t *slot = &state->slots[(h.pts_ms / AV_AUDIO_MS) % AUDIO_WINDOW];
    if (slot->valid && slot->pts == h.pts_ms) return false;
    slot->pts = h.pts_ms;
    memcpy(slot->block, wire + AV_HEADER_BYTES, sizeof slot->block);
    slot->valid = true;
    state->last_packet_us = now_us;
    return true;
}

bool audio_playout_timeline(const audio_playout_t *state, uint32_t session, int64_t now_us,
                            uint32_t *pts_ms, uint32_t *delay_us)
{
    if (!session || session != state->session || !state->deadline_us ||
        now_us - state->last_packet_us >= IDLE_US) return false;
    const int64_t delta = state->deadline_us - now_us;
    const uint64_t missed = delta < 0 ? ((uint64_t)-delta + BLOCK_US - 1) / BLOCK_US : 0;
    const uint64_t pts = state->expected + missed * AV_AUDIO_MS;
    if (pts > UINT32_MAX || (state->end_pts && pts >= state->end_pts)) return false;
    *pts_ms = (uint32_t)pts;
    *delay_us = (uint32_t)(delta + (int64_t)missed * BLOCK_US);
    return true;
}

audio_due_t audio_playout_take(audio_playout_t *state, int64_t now_us, uint8_t block[AV_AUDIO_BYTES])
{
    if (!state->session || !state->deadline_us || now_us < state->deadline_us) return AUDIO_WAIT;
    if (now_us - state->last_packet_us >= IDLE_US) {
        state->deadline_us = 0;
        if (state->end_pts) finish(state);
        return AUDIO_WAIT;
    }
    /* A stalled sink skips expired slots instead of bursting old audio. */
    int64_t missed = (now_us - state->deadline_us) / BLOCK_US;
    uint64_t pts = (uint64_t)state->expected + (uint64_t)missed * AV_AUDIO_MS;
    if (pts > UINT32_MAX - AV_AUDIO_MS || (state->end_pts && pts >= state->end_pts)) {
        finish(state);
        return AUDIO_WAIT;
    }
    state->started = true;
    state->deadline_us += (missed + 1) * BLOCK_US;
    state->expected = (uint32_t)pts + AV_AUDIO_MS;
    audio_slot_t *slot = &state->slots[(pts / AV_AUDIO_MS) % AUDIO_WINDOW];
    if (!slot->valid || slot->pts != pts) return AUDIO_SILENCE;
    memcpy(block, slot->block, sizeof slot->block);
    slot->valid = false;
    return AUDIO_BLOCK;
}
