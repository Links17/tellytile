#pragma once

#include "av_protocol.h"

typedef struct {
    unsigned next_stripe;
    uint32_t pts_ms;
} av_picture_t;

typedef enum { AV_PICTURE_INVALID, AV_PICTURE_INCOMPLETE, AV_PICTURE_COMPLETE } av_picture_result_t;
typedef bool (*av_inflate_stripe_t)(void *context, const uint8_t *input, size_t length, uint8_t *pixels);

av_picture_result_t av_picture_packet(av_picture_t *picture, uint8_t *pixels, const av_header_t *header,
                                      const uint8_t *payload, av_inflate_stripe_t inflate, void *context);
