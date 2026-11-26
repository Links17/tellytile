#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Local Indicator-to-reSpeaker sideband. FAV1 packets are unchanged. */
enum { RELAY_VOLUME_BYTES = 16, RELAY_VOLUME_SET = 1, RELAY_VOLUME_ACK = 2 };

typedef struct {
    uint32_t generation, revision;
    uint8_t level;
} relay_volume_t;

static inline void relay_volume_encode(uint8_t *wire, unsigned kind, relay_volume_t value)
{
    memcpy(wire, "FTV1", 4);
    wire[4] = kind;
    wire[5] = value.level;
    wire[6] = wire[7] = 0;
    for (unsigned i = 0; i < 4; ++i) {
        wire[8 + i] = value.generation >> (24 - i * 8);
        wire[12 + i] = value.revision >> (24 - i * 8);
    }
}

static inline bool relay_volume_decode(const uint8_t *wire, size_t size, unsigned kind,
                                        relay_volume_t *value)
{
    if (size != RELAY_VOLUME_BYTES || memcmp(wire, "FTV1", 4) || wire[4] != kind ||
        wire[5] > 100 || wire[6] || wire[7]) return false;
    *value = (relay_volume_t){.level = wire[5]};
    for (unsigned i = 0; i < 4; ++i) {
        value->generation = (value->generation << 8) | wire[8 + i];
        value->revision = (value->revision << 8) | wire[12 + i];
    }
    return value->generation && value->revision;
}

static inline bool relay_volume_matches(relay_volume_t a, relay_volume_t b)
{
    return a.generation == b.generation && a.revision == b.revision && a.level == b.level;
}

static inline bool relay_volume_newer(relay_volume_t value, relay_volume_t last)
{
    if (value.generation != last.generation) return true;
    uint32_t delta = value.revision - last.revision;
    return delta && delta < UINT32_C(0x80000000);
}
