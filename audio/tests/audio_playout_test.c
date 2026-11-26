#include "audio_playout.h"

#include <assert.h>
#include <stdio.h>

static bool send_block(audio_playout_t *s, uint32_t session, uint32_t pts, uint8_t marker, int64_t now)
{
    uint8_t wire[RELAY_AUDIO_BYTES] = {0};
    const av_header_t h = {.type = AV_AUDIO, .session = session, .pts_ms = pts,
                           .length = AV_AUDIO_BYTES};
    av_header_encode(wire, &h);
    wire[AV_HEADER_BYTES] = marker;
    return audio_playout_receive(s, wire, sizeof wire, now);
}

static void end(audio_playout_t *s, uint32_t session, uint32_t next_pts, int64_t now)
{
    uint8_t wire[AV_HEADER_BYTES];
    const av_header_t h = {.type = AV_END, .session = session, .pts_ms = next_pts};
    av_header_encode(wire, &h);
    assert(audio_playout_receive(s, wire, sizeof wire, now));
}

int main(void)
{
    audio_playout_t s = {0};
    uint8_t block[AV_AUDIO_BYTES];
    assert(send_block(&s, 1, 40, 2, 1000));
    assert(send_block(&s, 1, 0, 1, 2000));
    assert(send_block(&s, 1, 120, 4, 3000));
    assert(!send_block(&s, 1, 40, 99, 4000));
    assert(audio_playout_take(&s, 80000, block) == AUDIO_WAIT);
    assert(audio_playout_take(&s, 81000, block) == AUDIO_BLOCK && block[0] == 1);
    assert(audio_playout_take(&s, 121000, block) == AUDIO_BLOCK && block[0] == 2);
    assert(audio_playout_take(&s, 161000, block) == AUDIO_SILENCE);
    assert(!send_block(&s, 1, 80, 3, 171000));
    assert(audio_playout_take(&s, 201000, block) == AUDIO_BLOCK && block[0] == 4);
    assert(audio_playout_take(&s, 3004000, block) == AUDIO_WAIT);

    assert(send_block(&s, 1, 840, 7, 3005000)); /* Resume after a gap using a new anchor. */
    assert(audio_playout_take(&s, 3085000, block) == AUDIO_BLOCK && block[0] == 7);
    end(&s, 1, 0, 3087000);
    assert(!send_block(&s, 1, 880, 8, 3088000));
    s = (audio_playout_t){0};
    assert(send_block(&s, 2, 0, 9, 340000));
    assert(send_block(&s, 2, 80, 11, 341000));
    end(&s, 2, 120, 342000); /* END can precede a delayed tail packet. */
    assert(!send_block(&s, 2, 120, 99, 343000));
    assert(send_block(&s, 2, 40, 10, 344000));
    assert(audio_playout_take(&s, 420000, block) == AUDIO_BLOCK && block[0] == 9);
    assert(audio_playout_take(&s, 460000, block) == AUDIO_BLOCK && block[0] == 10);
    assert(audio_playout_take(&s, 500000, block) == AUDIO_BLOCK && block[0] == 11);
    assert(audio_playout_take(&s, 540000, block) == AUDIO_WAIT);
    assert(!send_block(&s, 2, 40, 10, 542000));

    assert(send_block(&s, 3, 0, 12, 550000));
    assert(send_block(&s, 3, 80, 14, 552000));
    assert(audio_playout_take(&s, 710000, block) == AUDIO_BLOCK && block[0] == 14);
    assert(audio_playout_take(&s, 710001, block) == AUDIO_WAIT); /* No stale burst. */
    assert(send_block(&s, 4, 0, 15, 730000));
    assert(!send_block(&s, 3, 120, 99, 731000));
    assert(audio_playout_take(&s, 810000, block) == AUDIO_BLOCK && block[0] == 15);
    s = (audio_playout_t){0};
    for (unsigned i = 0; i < 20; ++i)
        assert(send_block(&s, 6, i * AV_AUDIO_MS, i + 1, 1000 + i));
    for (unsigned i = 0; i < 20; ++i)
        assert(audio_playout_take(&s, 81000 + i * 40000, block) == AUDIO_BLOCK && block[0] == i + 1);
    puts("Audio playout passed reorder, loss, late drop, idle recovery, cancel, tail drain, and session turnover.");
}
