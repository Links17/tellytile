#pragma once

#include <stdbool.h>
#include <stdatomic.h>

/* One ESP event task publishes link changes. The low bit is IPv4 readiness;
 * the remaining bits distinguish sessions even if disconnect and reconnect
 * occur before the transport task polls again. */
static inline void relay_link_publish(atomic_uint *link, bool ready)
{
    const unsigned epoch = (atomic_load(link) & ~1u) + 2u;
    atomic_store(link, epoch | (ready ? 1u : 0u));
}

static inline bool relay_link_current(const atomic_uint *link, unsigned epoch)
{
    return (epoch & 1u) && atomic_load(link) == epoch;
}
