#include "av_protocol.h"
#include "reconnect_view.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_retry_and_recovery(void)
{
    atomic_uint state = 0;
    int64_t next = 0;
    assert(!tv_reconnect_due(atomic_load(&state), 0, &next));
    tv_view_advance(&state, TV_VIEW_TUNING);
    const unsigned original = tv_view_generation(atomic_load(&state));
    assert(!tv_reconnect_due(atomic_load(&state), 1, &next));
    tv_view_advance(&state, TV_VIEW_LINK_LOST);
    assert(tv_view_mode(atomic_load(&state)) == TV_VIEW_TUNING_FAILED);
    assert(tv_view_recovering(atomic_load(&state)));
    tv_view_advance(&state, TV_VIEW_KEEP);
    assert(tv_view_mode(atomic_load(&state)) == TV_VIEW_TUNING_FAILED);
    tv_view_presented(&state, original);
    tv_view_connected(&state);
    assert(tv_view_mode(atomic_load(&state)) == TV_VIEW_TUNING);
    tv_view_presented(&state, tv_view_generation(atomic_load(&state)));
    assert(tv_view_mode(atomic_load(&state)) == TV_VIEW_PICTURE);
    tv_view_advance(&state, TV_VIEW_LINK_LOST);
    assert(tv_reconnect_due(atomic_load(&state), 2, &next));
    assert(!tv_reconnect_due(atomic_load(&state), 250001, &next));
    assert(tv_reconnect_due(atomic_load(&state), 250002, &next));

    tv_view_advance(&state, TV_VIEW_KEEP); /* New connection keeps animating until a presentation. */
    const unsigned retry = tv_view_generation(atomic_load(&state));
    tv_view_presented(&state, original); /* A late old frame cannot clear the notice. */
    assert(tv_reconnect_due(atomic_load(&state), 500002, &next));
    tv_view_presented(&state, retry);
    assert(!tv_reconnect_due(atomic_load(&state), 500003, &next) && next == 0);
    tv_view_advance(&state, TV_VIEW_LINK_LOST);
    tv_view_presented(&state, retry);
    assert(tv_reconnect_due(atomic_load(&state), 500004, &next));
    assert(tv_reconnect_due(atomic_load(&state), 1500000, &next));
    assert(!tv_reconnect_due(atomic_load(&state), 1500001, &next)); /* No catch-up burst. */
    const unsigned disconnected = tv_view_generation(atomic_load(&state));
    tv_view_advance(&state, TV_VIEW_PICTURE); /* Pause. */
    const unsigned paused = atomic_load(&state);
    assert(tv_view_generation(paused) != disconnected);
    assert(!tv_reconnect_due(paused, 1500002, &next) && next == 0);
    tv_view_presented(&state, disconnected);
    assert(atomic_load(&state) == paused);
    tv_view_advance(&state, TV_VIEW_LINK_LOST);
    assert(tv_reconnect_due(atomic_load(&state), 1500003, &next));
    const unsigned old = tv_view_generation(atomic_load(&state));
    tv_view_advance(&state, TV_VIEW_TUNING);
    tv_view_presented(&state, old);
    assert(tv_view_mode(atomic_load(&state)) == TV_VIEW_TUNING);
    assert(!tv_reconnect_due(atomic_load(&state), 1500004, &next) && next == 0);
}

static void test_snow(void)
{
    enum { W = AV_VIDEO_WIDTH, H = AV_VIDEO_HEIGHT };
    static uint16_t first[W * H], second[W * H];
    uint32_t seed = 0x49ae37cb;
    tv_reconnect_draw(first, &seed);
    tv_reconnect_draw(second, &seed);
    assert(memcmp(first, second, sizeof first));
    unsigned white = 0;
    for (int i = 0; i < W * H; ++i) {
        assert(first[i] == 0 || first[i] == UINT16_MAX);
        white += first[i] == UINT16_MAX;
    }
    assert(white > W * H / 3 && white < W * H * 2 / 3);
    for (int y = 0; y < H / 4; y += 2) {
        for (int x = 0; x < W; x += 2) {
            assert(first[y * W + x] == first[y * W + x + 1]);
            assert(first[y * W + x] == first[(y + 1) * W + x]);
        }
    }
    const int text_top = (H - 7) / 2, text_left = (W - 89) / 2;
    assert(first[(text_top - 1) * W + text_left] == 0);
    assert(first[text_top * W + text_left] == UINT16_MAX);
    assert(first[text_top * W + text_left + 6] == UINT16_MAX);
    assert(!memcmp(first + text_top * W + text_left, second + text_top * W + text_left,
                   89 * sizeof *first));
}

static void test_tuning_bars(void)
{
    enum { W = AV_VIDEO_WIDTH, H = AV_VIDEO_HEIGHT };
    static uint16_t opening[W * H], failed[W * H];
    const uint16_t colors[] = {0xffff, 0xffe0, 0x07ff, 0x07e0, 0xf81f, 0xf800, 0x001f};
    tv_tuning_draw(opening, TV_VIEW_TUNING);
    tv_tuning_draw(failed, TV_VIEW_TUNING_FAILED);
    for (unsigned bar = 0; bar < 7; ++bar) {
        for (unsigned x = W * bar / 7; x < W * (bar + 1) / 7; ++x) {
            assert(opening[x] == colors[bar]);
            assert(failed[(H - 1) * W + x] == colors[bar]);
        }
    }
    assert(memcmp(opening + (H / 2 - 10) * W, failed + (H / 2 - 10) * W, 20 * W * sizeof *opening));
}

int main(void)
{
    test_retry_and_recovery();
    test_snow();
    test_tuning_bars();
    puts("Waiting and reconnect views passed tuning bars, failure notice, 4 fps snow pacing, stale-frame rejection, first-frame recovery and deliberate pause or retune.");
}
