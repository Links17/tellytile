#ifndef LIVE_TV_RELAY_AUDIO_H
#define LIVE_TV_RELAY_AUDIO_H

#include "av_protocol.h"

enum { RELAY_AUDIO_BYTES = AV_HEADER_BYTES + AV_AUDIO_BYTES };

static inline bool relay_audio_header(const uint8_t *wire, size_t size, av_header_t *header)
{
    return size == RELAY_AUDIO_BYTES && av_header_decode(wire, header)
        && header->type == AV_AUDIO && header->session
        && header->pts_ms % AV_AUDIO_MS == 0 && header->pts_ms <= UINT32_MAX - AV_AUDIO_MS
        && av_adpcm_header_valid(wire + AV_HEADER_BYTES);
}

static inline void relay_audio_decode(const uint8_t *block, int16_t mono[AV_AUDIO_SAMPLES],
                                      int32_t stereo[AV_AUDIO_SAMPLES * 2])
{
    av_adpcm_decode(block, AV_AUDIO_SAMPLES, mono);
    for (size_t i = 0; i < AV_AUDIO_SAMPLES; ++i)
        stereo[2 * i] = stereo[2 * i + 1] = (int32_t)mono[i] * 65536;
}

#endif
