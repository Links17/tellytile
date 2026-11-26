#pragma once

#include "relay_timing.h"

/* Local fallback uses the same 80 ms startup buffer as the audio receiver. */
enum { RELAY_START_US = 80000, VIDEO_LATE_US = 120000, VIDEO_AHEAD_US = 1200000 };

typedef struct {
    bool anchored, audio_anchored, remote;
    uint32_t first_pts;
    int64_t first_due_us;
    int correction_us;
} video_clock_t;

static inline int64_t video_clock_base(const video_clock_t *clock, uint32_t pts)
{
    return clock->first_due_us + ((int64_t)pts - clock->first_pts) * 1000;
}

static inline void video_clock_start(video_clock_t *clock, uint32_t pts, int64_t now_us)
{
    if (clock->anchored) return;
    clock->anchored = true;
    clock->first_pts = pts;
    clock->first_due_us = now_us + RELAY_START_US;
}

static inline void video_clock_audio(video_clock_t *clock, uint32_t pts, int64_t now_us)
{
    if (clock->audio_anchored) return;
    video_clock_start(clock, pts, now_us);
    clock->audio_anchored = true;
    const int64_t delta = now_us + RELAY_START_US - video_clock_base(clock, pts);
    if (delta >= -VIDEO_AHEAD_US && delta <= VIDEO_AHEAD_US) clock->correction_us = (int)delta;
}

static inline bool video_clock_sync(video_clock_t *clock, relay_timing_t reply,
                                    uint32_t session, uint32_t token, int64_t sent_us, int64_t received_us)
{
    const int64_t rtt = received_us - sent_us;
    if (!clock->audio_anchored || !session || !token || reply.token != token || reply.session != session ||
        reply.delay_us > RELAY_TIMING_MAX_DELAY_US || rtt < 0 || rtt > RELAY_TIMING_MAX_RTT_US)
        return false;
    const int64_t target = sent_us + rtt / 2 + reply.delay_us - video_clock_base(clock, reply.pts_ms);
    if (target < -3000000 || target > 3000000) return false;
    const int change = (int)target - clock->correction_us;
    if (!clock->remote || change > VIDEO_LATE_US || change < -VIDEO_LATE_US)
        clock->correction_us = (int)target;
    else
        clock->correction_us += change / 4;
    clock->remote = true;
    return true;
}

static inline bool video_clock_due(const video_clock_t *clock, uint32_t pts, int64_t now_us,
                                   int64_t *base_us)
{
    *base_us = clock->anchored ? video_clock_base(clock, pts) : now_us;
    const int64_t due = *base_us + clock->correction_us;
    return due >= now_us - VIDEO_LATE_US && due <= now_us + VIDEO_AHEAD_US;
}
