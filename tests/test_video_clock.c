#include "relay_link.h"
#include <limits.h>
#include "video_clock.h"
#include "audio_playout.h"

#include <assert.h>
#include <stdio.h>

static void audio(audio_playout_t *speaker, uint32_t session, uint32_t pts, int64_t now)
{
    uint8_t bytes[RELAY_AUDIO_BYTES] = {0};
    av_header_encode(bytes, &(av_header_t){.type = AV_AUDIO, .session = session,
        .pts_ms = pts, .length = AV_AUDIO_BYTES});
    assert(audio_playout_receive(speaker, bytes, sizeof bytes, now));
}

static void paired_clock(void)
{
    enum { SESSION = 99, TOKEN = 11, SPEAKER_OFFSET = 9000000 };
    video_clock_t picture = {0};
    audio_playout_t speaker = {0};
    video_clock_audio(&picture, 0, 1000000);
    /* Audio PTS 0 was emitted but never received by reSpeaker. */
    audio(&speaker, SESSION, 40, SPEAKER_OFFSET + 1005000);
    uint8_t wire[RELAY_TIMING_BYTES];
    relay_timing_encode(wire, (relay_timing_t){.token = TOKEN, .session = SESSION,
        .delay_us = RELAY_TIMING_PROBE});
    const uint8_t expected[] = {'F','T','S','1', 0,0,0,TOKEN, 0,0,0,SESSION, 0,0,0,0, 255,255,255,255};
    assert(!memcmp(wire, expected, sizeof expected));
    relay_timing_t request;
    assert(!relay_timing_decode(wire, sizeof wire - 1, &request));
    assert(relay_timing_decode(wire, sizeof wire, &request));
    assert(!audio_playout_timeline(&speaker, SESSION + 1, SPEAKER_OFFSET + 1025000,
                                   &request.pts_ms, &request.delay_us));
    assert(audio_playout_timeline(&speaker, SESSION, SPEAKER_OFFSET + 1025000,
                                  &request.pts_ms, &request.delay_us));
    assert(request.pts_ms == 40 && request.delay_us == 60000);
    relay_timing_encode(wire, request);
    relay_timing_t reply;
    assert(relay_timing_decode(wire, sizeof wire, &reply));
    assert(!video_clock_sync(&picture, reply, SESSION + 1, TOKEN, 1020000, 1030000));
    assert(!video_clock_sync(&picture, reply, SESSION, TOKEN + 1, 1020000, 1030000));
    assert(!video_clock_sync(&picture, reply, SESSION, TOKEN, 1020000, 1061000));
    assert(video_clock_sync(&picture, reply, SESSION, TOKEN, 1020000, 1030000));
    assert(picture.remote && picture.correction_us == -35000);
    assert(video_clock_base(&picture, 40) + picture.correction_us == 1085000);

    /* A small asymmetric sample adjusts gradually, a real re-anchor moves queued frames. */
    assert(video_clock_sync(&picture, reply, SESSION, TOKEN, 1024000, 1034000));
    assert(picture.correction_us == -34000);
    int64_t base;
    assert(video_clock_due(&picture, 80, 1050000, &base) && base == 1160000);
    assert(audio_playout_timeline(&speaker, SESSION, SPEAKER_OFFSET + 1110000,
                                  &reply.pts_ms, &reply.delay_us));
    assert(reply.pts_ms == 80 && reply.delay_us == 15000);
    assert(!audio_playout_timeline(&speaker, SESSION, SPEAKER_OFFSET + 4005000,
                                   &reply.pts_ms, &reply.delay_us));
    audio(&speaker, SESSION, 2800, SPEAKER_OFFSET + 4006000);
    assert(audio_playout_timeline(&speaker, SESSION, SPEAKER_OFFSET + 4010000,
                                  &reply.pts_ms, &reply.delay_us));
    assert(video_clock_sync(&picture, reply, SESSION, TOKEN, 4008000, 4012000));
    assert(picture.correction_us == 206000);
    assert(video_clock_base(&picture, 2800) + picture.correction_us == 4086000);
    assert(!video_clock_due(&picture, 0, 4000000, &base)); /* Stale queued frames expire. */
}

int main(void)
{
    atomic_uint link = 0;
    assert(!relay_link_current(&link, atomic_load(&link)));
    relay_link_publish(&link, true);
    const unsigned first_link = atomic_load(&link);
    assert(relay_link_current(&link, first_link));
    relay_link_publish(&link, false);
    assert(!relay_link_current(&link, first_link));
    relay_link_publish(&link, true);
    assert(!relay_link_current(&link, first_link));
    const unsigned restored_link = atomic_load(&link);
    assert(relay_link_current(&link, restored_link));
    relay_link_publish(&link, true);
    assert(!relay_link_current(&link, restored_link));
    atomic_store(&link, UINT_MAX);
    relay_link_publish(&link, false);
    assert(!relay_link_current(&link, UINT_MAX));
    relay_link_publish(&link, true);
    assert(relay_link_current(&link, atomic_load(&link)));
    puts("Both relay owners invalidate the old socket when Wi-Fi disconnects and rejoins between polls.");

    video_clock_t clock = {0};
    int64_t due;
    assert(video_clock_due(&clock, 50, 100000, &due) && due == 100000);
    video_clock_audio(&clock, 40, 100000);
    assert(video_clock_due(&clock, 40, 110000, &due) && due == 180000);
    assert(video_clock_due(&clock, 0, 110000, &due) && due == 140000);
    video_clock_audio(&clock, 120, 101500); /* Burst arrivals preserve the anchor. */
    assert(video_clock_due(&clock, 120, 160000, &due) && due == 260000);
    assert(!video_clock_due(&clock, 120, 381000, &due));
    assert(!video_clock_due(&clock, 4000, 160000, &due));
    clock = (video_clock_t){0};
    video_clock_audio(&clock, 0, 1000000);
    assert(video_clock_due(&clock, 40, 1010000, &due) && due == 1120000);
    clock = (video_clock_t){0};
    video_clock_start(&clock, 500, 1500000);
    video_clock_start(&clock, 625, 1501000);
    assert(video_clock_due(&clock, 625, 1501000, &due) && due == 1705000);
    video_clock_audio(&clock, 480, 1504000);
    assert(clock.correction_us == 24000 && video_clock_base(&clock, 500) == 1580000);
    paired_clock();
    puts("Shared relay timing passed video-only pacing, lost first audio packet, invalid/reordered probes, bounded smoothing and receiver re-anchor.");
}
