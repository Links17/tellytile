/* Feed a pinned FoloTV session through the same validators and frame assembly as the firmware. */
#include "av_adpcm.h"
#include "av_picture.h"
#include "playback_profile.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <zlib.h>

static uint8_t payload[AV_VIDEO_MAX + 1];
static uint8_t picture_pixels[AV_VIDEO_WIDTH * AV_VIDEO_HEIGHT];
static uint8_t rgb565_be[2 * AV_VIDEO_WIDTH * AV_VIDEO_HEIGHT];

static bool inflate_stripe(void *context, const uint8_t *input, size_t length, uint8_t *pixels)
{
    (void)context;
    unsigned long consumed = length, produced = AV_STRIPE_PIXELS;
    return uncompress2(pixels, &produced, input, &consumed) == Z_OK &&
           consumed == length && produced == AV_STRIPE_PIXELS;
}

static bool report_packet(unsigned profile, av_picture_t *picture, uint16_t *palette,
                          const av_header_t *header)
{
    if (header->type == AV_CONFIG) {
        bool supports_profiles = false;
        cJSON *config = cJSON_ParseWithLengthOpts((char *)payload, header->length + 1, NULL, true);
        const bool valid = tv_session_config_valid(config, header->session, profile, &supports_profiles);
        cJSON_Delete(config);
        return valid && supports_profiles;
    }
    if (header->type == AV_PALETTE) {
        av_palette_decode(payload, palette);
    } else if (header->type == AV_VIDEO) {
        const av_picture_result_t result = av_picture_packet(picture, picture_pixels, header, payload, inflate_stripe, NULL);
        if (result == AV_PICTURE_INVALID) return false;
        if (result == AV_PICTURE_COMPLETE) {
            for (size_t i = 0; i < sizeof picture_pixels; ++i) {
                const uint16_t rgb = palette[picture_pixels[i]];
                rgb565_be[2 * i] = rgb >> 8;
                rgb565_be[2 * i + 1] = (uint8_t)rgb;
            }
            printf("video %" PRIu32 " %08lx\n", picture->pts_ms, crc32(0, rgb565_be, sizeof rgb565_be));
        }
    } else if (header->type == AV_AUDIO) {
        int16_t pcm[AV_AUDIO_SAMPLES];
        uint8_t little_endian[sizeof pcm];
        if (!av_adpcm_header_valid(payload)) return false;
        av_adpcm_decode(payload, AV_AUDIO_SAMPLES, pcm);
        for (size_t i = 0; i < AV_AUDIO_SAMPLES; ++i) {
            little_endian[i * 2] = (uint8_t)pcm[i];
            little_endian[i * 2 + 1] = (uint16_t)pcm[i] >> 8;
        }
        printf("audio %" PRIu32 " %08lx\n", header->pts_ms, crc32(0, little_endian, sizeof little_endian));
    }
    return true;
}

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    unsigned profile = 0;
    while (profile < TV_PROFILE_COUNT && strcmp(argv[1], tv_profiles[profile].id)) ++profile;
    if (profile == TV_PROFILE_COUNT) return 2;
    av_stream_t stream = {0};
    av_picture_t picture = {0};
    uint16_t palette[AV_PALETTE_ENTRIES];
    bool palette_ready = false;
    uint8_t wire[AV_HEADER_BYTES];
    size_t count;
    while ((count = fread(wire, 1, sizeof wire, stdin))) {
        av_header_t header;
        if (count != sizeof wire || !av_header_decode(wire, &header) || !av_stream_accept(&stream, &header) ||
            header.length >= sizeof payload || fread(payload, 1, header.length, stdin) != header.length) {
            fputs("Native FAV1 header or payload rejected\n", stderr);
            return 1;
        }
        if ((header.type == AV_VIDEO || header.type == AV_AUDIO) && !palette_ready) return 1;
        if (header.type == AV_PALETTE) palette_ready = true;
        payload[header.length] = 0;
        if (!report_packet(profile, &picture, palette, &header)) {
            fprintf(stderr, "Native FAV1 packet %" PRIu32 " rejected\n", header.seq);
            return 1;
        }
    }
    return ferror(stdin) || !stream.configured;
}
