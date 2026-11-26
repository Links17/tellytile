#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>

typedef enum {
    TV_VIEW_PICTURE, TV_VIEW_TUNING, TV_VIEW_TUNING_FAILED, TV_VIEW_RECONNECTING,
    TV_VIEW_KEEP, TV_VIEW_LINK_LOST,
} tv_view_mode_t;
enum { TV_VIEW_MASK = 3u, TV_NOTICE_INTERVAL_US = 250000 };

static inline unsigned tv_view_generation(unsigned state)
{
    return state & ~(unsigned)TV_VIEW_MASK;
}

static inline tv_view_mode_t tv_view_mode(unsigned state)
{
    return (tv_view_mode_t)(state & TV_VIEW_MASK);
}

/* Retire old queued frames while keeping a notice until the new session
 * presents a picture. A deliberate channel change starts a new tuning view. */
static inline void tv_view_advance(atomic_uint *view, tv_view_mode_t requested)
{
    unsigned state = atomic_load(view);
    for (;;) {
        tv_view_mode_t mode = requested;
        const tv_view_mode_t previous = tv_view_mode(state);
        if (requested == TV_VIEW_KEEP) mode = previous;
        if (requested == TV_VIEW_LINK_LOST)
            mode = previous == TV_VIEW_TUNING || previous == TV_VIEW_TUNING_FAILED
                ? TV_VIEW_TUNING_FAILED : TV_VIEW_RECONNECTING;
        const unsigned next = (tv_view_generation(state) + TV_VIEW_MASK + 1u) | mode;
        if (atomic_compare_exchange_weak(view, &state, next)) return;
    }
}

static inline bool tv_view_recovering(unsigned state)
{
    const tv_view_mode_t mode = tv_view_mode(state);
    return mode == TV_VIEW_TUNING_FAILED || mode == TV_VIEW_RECONNECTING;
}

static inline void tv_view_connected(atomic_uint *view)
{
    unsigned state = atomic_load(view);
    if (tv_view_mode(state) == TV_VIEW_TUNING_FAILED) {
        const unsigned next = tv_view_generation(state) | TV_VIEW_TUNING;
        atomic_compare_exchange_strong(view, &state, next);
    }
}

static inline void tv_view_presented(atomic_uint *view, unsigned generation)
{
    unsigned state = atomic_load(view);
    if (tv_view_generation(state) == generation)
        atomic_compare_exchange_strong(view, &state, generation);
}

bool tv_reconnect_due(unsigned view, int64_t now, int64_t *next);
void tv_reconnect_draw(uint16_t *pixels, uint32_t *seed);
void tv_tuning_draw(uint16_t *pixels, tv_view_mode_t mode);
