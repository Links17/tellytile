#pragma once

#include <stdint.h>
#include <string.h>

#include "channel_menu.h"

/* FAV1 is landscape and the Indicator is square. Scale the centered square
 * at the output stage so the wire format and playback timing stay intact. */
static inline void tv_fill_video_rows(uint16_t *output, const uint16_t *video,
                                      uint16_t *line, int top, int bottom)
{
    enum { SCREEN = TV_MENU_WIDTH, CROP_LEFT = (AV_VIDEO_WIDTH - AV_VIDEO_HEIGHT) / 2 };
    int previous = -1;
    for (int y = top; y < bottom; ++y) {
        const int source_y = y * AV_VIDEO_HEIGHT / SCREEN;
        if (source_y != previous) {
            const uint16_t *source = video + source_y * AV_VIDEO_WIDTH + CROP_LEFT;
            for (int x = 0; x < SCREEN; ++x) line[x] = source[x * AV_VIDEO_HEIGHT / SCREEN];
            previous = source_y;
        }
        memcpy(output + y * SCREEN, line, SCREEN * sizeof(*line));
    }
}
