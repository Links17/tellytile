#include "relay_audio.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
    uint8_t datagram[RELAY_AUDIO_BYTES] = {0};
    av_header_t expected = {.type = AV_AUDIO, .session = 17, .seq = 42, .pts_ms = 80, .length = AV_AUDIO_BYTES};
    av_header_t actual;
    av_header_encode(datagram, &expected);
    uint8_t *block = datagram + AV_HEADER_BYTES;
    block[0] = 0x20; // positive initial predictor
    assert(relay_audio_header(datagram, sizeof datagram, &actual));
    assert(actual.session == 17 && actual.pts_ms == 80);
    int16_t mono[AV_AUDIO_SAMPLES];
    int32_t stereo[AV_AUDIO_SAMPLES * 2];
    relay_audio_decode(block, mono, stereo);
    for (size_t i = 0; i < AV_AUDIO_SAMPLES; ++i) {
        assert(mono[i] > 0 && stereo[i * 2] == (int32_t)mono[i] * 65536);
        assert(stereo[i * 2] == stereo[i * 2 + 1]);
    }
    assert(!relay_audio_header(datagram, sizeof datagram - 1, &actual));
    block[2] = AV_ADPCM_INDEX_MAX + 1; // invalid IMA state
    assert(!relay_audio_header(datagram, sizeof datagram, &actual));
    block[2] = 0;
    ++expected.pts_ms;
    av_header_encode(datagram, &expected);
    assert(!relay_audio_header(datagram, sizeof datagram, &actual));
    puts("Native relay datagram validity and exact stereo PCM conversion passed.");
}
