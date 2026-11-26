#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Local UDP sideband. A probe has a session and token, but no schedule. */
enum { RELAY_TIMING_BYTES = 20, RELAY_TIMING_MAX_DELAY_US = 80000, RELAY_TIMING_MAX_RTT_US = 40000 };
#define RELAY_TIMING_PROBE UINT32_MAX

typedef struct {
    uint32_t token, session, pts_ms, delay_us;
} relay_timing_t;

static inline void relay_timing_encode(uint8_t wire[RELAY_TIMING_BYTES], relay_timing_t message)
{
    const uint32_t fields[] = {message.token, message.session, message.pts_ms, message.delay_us};
    memcpy(wire, "FTS1", 4);
    for (unsigned field = 0; field < 4; ++field)
        for (unsigned byte = 0; byte < 4; ++byte)
            wire[4 + field * 4 + byte] = fields[field] >> (24 - byte * 8);
}

static inline bool relay_timing_decode(const uint8_t *wire, size_t size, relay_timing_t *message)
{
    if (size != RELAY_TIMING_BYTES || memcmp(wire, "FTS1", 4)) return false;
    uint32_t fields[4] = {0};
    for (unsigned field = 0; field < 4; ++field)
        for (unsigned byte = 0; byte < 4; ++byte)
            fields[field] = (fields[field] << 8) | wire[4 + field * 4 + byte];
    if (!fields[0] || !fields[1] ||
        (fields[3] == RELAY_TIMING_PROBE ? fields[2] != 0 : fields[3] > RELAY_TIMING_MAX_DELAY_US))
        return false;
    *message = (relay_timing_t){fields[0], fields[1], fields[2], fields[3]};
    return true;
}
