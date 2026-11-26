#include "reconnect_view.h"

#include "av_protocol.h"
#include "setup_font.h"

#include <string.h>

bool tv_reconnect_due(unsigned view, int64_t now, int64_t *next)
{
    if (tv_view_mode(view) != TV_VIEW_RECONNECTING) {
        *next = 0;
        return false;
    }
    if (now < *next) return false;
    *next = now + TV_NOTICE_INTERVAL_US;
    return true;
}

static void draw_message(uint16_t *pixels, const char *title)
{
    enum { W = AV_VIDEO_WIDTH, H = AV_VIDEO_HEIGHT, ADVANCE = 6, GLYPH_H = 7, PAD = 8 };
    const int text_w = (int)strlen(title) * ADVANCE - 1;
    const int left = (W - text_w) / 2, top = (H - GLYPH_H) / 2;
    for (int y = top - PAD; y < top + GLYPH_H + PAD; ++y)
        memset(pixels + y * W + left - PAD, 0, (text_w + PAD * 2) * sizeof *pixels);
    for (int i = 0; title[i]; ++i) {
        const uint8_t *bitmap = glyph((unsigned char)title[i]);
        for (int x = 0; x < 5; ++x) {
            for (int y = 0; y < GLYPH_H; ++y) {
                if (bitmap[x] & (1u << y)) pixels[(top + y) * W + left + i * ADVANCE + x] = UINT16_MAX;
            }
        }
    }
}

void tv_reconnect_draw(uint16_t *pixels, uint32_t *seed)
{
    enum { W = AV_VIDEO_WIDTH, H = AV_VIDEO_HEIGHT };
    _Static_assert(W % 2 == 0 && H % 2 == 0, "The notice renders 2x2 dots");
    for (int y = 0; y < H; y += 2) {
        for (int x = 0; x < W; x += 2) {
            /* Local visual noise. Independent of the real audio and video. */
            *seed ^= *seed << 13;
            *seed ^= *seed >> 17;
            *seed ^= *seed << 5;
            const uint16_t color = (*seed & 1) ? UINT16_MAX : 0;
            pixels[y * W + x] = pixels[y * W + x + 1] = color;
            pixels[(y + 1) * W + x] = pixels[(y + 1) * W + x + 1] = color;
        }
    }
    draw_message(pixels, "RECONNECTING...");
}

void tv_tuning_draw(uint16_t *pixels, tv_view_mode_t mode)
{
    enum { W = AV_VIDEO_WIDTH, H = AV_VIDEO_HEIGHT };
    static const uint16_t colors[] = {0xffff, 0xffe0, 0x07ff, 0x07e0, 0xf81f, 0xf800, 0x001f};
    const int count = sizeof colors / sizeof colors[0];
    for (int bar = 0; bar < count; ++bar) {
        const int left = W * bar / count, right = W * (bar + 1) / count;
        for (int y = 0; y < H; ++y) {
            for (int x = left; x < right; ++x) pixels[y * W + x] = colors[bar];
        }
    }
    draw_message(pixels, mode == TV_VIEW_TUNING_FAILED ? "WAITING TO RETRY" : "OPENING CHANNEL...");
}
