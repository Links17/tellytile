#include "av_picture.h"

av_picture_result_t av_picture_packet(av_picture_t *picture, uint8_t *pixels, const av_header_t *header,
                                      const uint8_t *payload, av_inflate_stripe_t inflate, void *context)
{
    av_video_t stripes;
    if (!av_video_decode(payload, header->length, &stripes)) return AV_PICTURE_INVALID;
    if (header->flags & AV_VIDEO_CONTINUES) {
        if (!picture->next_stripe || stripes.first != picture->next_stripe || header->pts_ms != picture->pts_ms)
            return AV_PICTURE_INVALID;
    } else {
        if (stripes.first || picture->next_stripe) return AV_PICTURE_INVALID;
        picture->pts_ms = header->pts_ms;
    }
    for (unsigned i = 0; i < stripes.count; ++i) {
        const uint8_t *data;
        size_t len;
        if (!av_video_stripe(&stripes, i, &data, &len)) return AV_PICTURE_INVALID;
        uint8_t *out = pixels + (stripes.first + i) * AV_STRIPE_PIXELS;
        if (len && !inflate(context, data, len, out)) return AV_PICTURE_INVALID;
    }
    picture->next_stripe = stripes.first + stripes.count;
    if (picture->next_stripe != AV_STRIPES) return AV_PICTURE_INCOMPLETE;
    picture->next_stripe = 0;
    return AV_PICTURE_COMPLETE;
}
