#include "channel_menu.h"
#include "av_protocol.h"
#include "playback_profile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { int x, y, w, h; } rectangle_t;
static const rectangle_t top_channel = {16, 14, 224, 52};
static const rectangle_t top_pause = {256, 14, 80, 52};
static const rectangle_t resume = {16, 354, 216, 73};
static const rectangle_t setup = {248, 354, 216, 73};
static const rectangle_t status_back = {16, 420, 216, 52};
static const rectangle_t status_setup = {248, 420, 216, 52};
static const rectangle_t status_band = {0, TV_STATUS_Y, TV_MENU_WIDTH, TV_STATUS_ROWS};
static const rectangle_t top_quality = {352, 14, 112, 52};
static const rectangle_t previous = {16, 438, 140, 34};
static const rectangle_t next = {324, 438, 140, 34};
static const rectangle_t tiles[] = {
    {16, 342, 216, 41}, {248, 342, 216, 41},
    {16, 389, 216, 41}, {248, 389, 216, 41},
};
static const rectangle_t adjustment[] = {
    {16, 342, 110, 56}, {354, 342, 110, 56},
    {16, 407, 110, 56}, {354, 407, 110, 56},
};
static const rectangle_t profiles[TV_PROFILE_COUNT] = {
    {16, 354, 140, 73}, {170, 354, 140, 73}, {324, 354, 140, 73},
};

static bool contains(rectangle_t area, int x, int y)
{
    return x >= area.x && x < area.x + area.w && y >= area.y && y < area.y + area.h;
}

int64_t tv_menu_deadline(tv_menu_view_t view, int64_t now_us)
{
    if (view == TV_MENU_ADJUSTMENTS) return now_us + 6000000;
    if (view == TV_MENU_STATUS) return now_us + 8000000;
    if (view == TV_MENU_CHANNELS || view == TV_MENU_PROFILES) return now_us + 12000000;
    return 0;
}

bool tv_menu_expired(tv_menu_view_t view, int64_t deadline_us, int64_t now_us)
{
    return deadline_us != 0 && now_us >= deadline_us && view != TV_MENU_HIDDEN && view != TV_MENU_PAUSED;
}

unsigned tv_menu_page_count(unsigned count)
{
    return (count + TV_MENU_PAGE_ITEMS - 1) / TV_MENU_PAGE_ITEMS;
}

bool tv_touch_sample(tv_touch_tracker_t *tracker, bool valid, bool contact,
                     int x, int y, tv_touch_event_t *event)
{
    if (!valid) {
        tracker->interrupted = true;
        return false;
    }
    if (contact) {
        if (!tracker->pressed) {
            tracker->x = x;
            tracker->y = y;
            tracker->pressed = true;
        }
        tracker->dx = x - tracker->x;
        tracker->dy = y - tracker->y;
        const unsigned dx = abs(tracker->dx), dy = abs(tracker->dy);
        if (dx > tracker->max_x) tracker->max_x = dx;
        if (dy > tracker->max_y) tracker->max_y = dy;
        return false;
    }
    const bool complete = tracker->pressed && !tracker->interrupted;
    *event = (tv_touch_event_t){.x = tracker->x, .y = tracker->y, .kind = TV_TOUCH_TAP};
    bool recognized = complete && tracker->max_x <= 24 && tracker->max_y <= 24;
    if (complete && tracker->max_x <= 40 && abs(tracker->dy) >= 56) {
        event->kind = tracker->dy < 0 ? TV_TOUCH_SWIPE_UP : TV_TOUCH_SWIPE_DOWN;
        recognized = true;
    }
    *tracker = (tv_touch_tracker_t){0};
    return recognized;
}

unsigned tv_menu_restore_page(unsigned page, unsigned count, unsigned current, bool visited)
{
    const unsigned pages = tv_menu_page_count(count);
    if (!pages) return 0;
    if (!visited) return current < count ? current / TV_MENU_PAGE_ITEMS : 0;
    return page < pages ? page : pages - 1;
}

tv_menu_action_t tv_menu_touch(tv_menu_view_t view, unsigned page, unsigned count,
                               tv_touch_event_t event, unsigned *selected)
{
    if (event.kind == TV_TOUCH_TAP) return tv_menu_hit(view, page, count, event.x, event.y, selected);

    const int height = TV_MENU_BOTTOM_Y + TV_MENU_BOTTOM_ROWS;
    const rectangle_t video = view == TV_MENU_HIDDEN
        ? (rectangle_t){0, 0, TV_MENU_WIDTH, height}
        : (rectangle_t){0, TV_STATUS_Y + TV_STATUS_ROWS, TV_MENU_WIDTH,
                        TV_MENU_BOTTOM_Y - TV_STATUS_Y - TV_STATUS_ROWS};
    if ((view != TV_MENU_HIDDEN && view != TV_MENU_ADJUSTMENTS) ||
        count < 2 || !contains(video, event.x, event.y))
        return TV_MENU_NONE;
    return event.kind == TV_TOUCH_SWIPE_UP ? TV_MENU_CHANNEL_FORWARD : TV_MENU_CHANNEL_BACK;
}

bool tv_channel_move(const char *const *ids, unsigned count, char *id, size_t capacity, int direction)
{
    if (!ids || !id || !count || capacity < 2 || (direction != 1 && direction != -1)) return false;
    unsigned index = av_channel_index_of(ids, count, id);
    if (index == count) index = direction > 0 ? count - 1 : 0;
    char next[AV_CHANNEL_ID_MAX];
    if (!av_channel_step(ids, count, &index, direction, next, sizeof next) || !strcmp(id, next))
        return false;
    const size_t length = strlen(next) + 1;
    if (length > capacity) return false;
    memcpy(id, next, length);
    return true;
}

tv_menu_action_t tv_menu_hit(tv_menu_view_t view, unsigned page, unsigned count, int x, int y, unsigned *selected)
{
    if (view == TV_MENU_PAUSED) {
        if (contains(resume, x, y)) return TV_MENU_RESUME;
        if (contains(setup, x, y)) return TV_MENU_SETUP;
        return TV_MENU_NONE;
    }
    if (view == TV_MENU_HIDDEN) {
        const rectangle_t screen = {0, 0, TV_MENU_WIDTH, TV_MENU_BOTTOM_Y + TV_MENU_BOTTOM_ROWS};
        return contains(screen, x, y) ? TV_MENU_SHOW : TV_MENU_NONE;
    }
    if (contains(top_pause, x, y)) return TV_MENU_PAUSE;
    if (contains(status_band, x, y)) return TV_MENU_TOGGLE_STATUS;
    if (view == TV_MENU_STATUS) {
        if (contains(status_back, x, y)) return TV_MENU_TOGGLE_STATUS;
        if (contains(status_setup, x, y)) return TV_MENU_SETUP;
        return TV_MENU_NONE;
    }
    if (contains(top_channel, x, y)) return TV_MENU_TOGGLE;
    if (contains(top_quality, x, y)) return TV_MENU_QUALITY;
    if (view == TV_MENU_PROFILES) {
        for (unsigned i = 0; i < TV_PROFILE_COUNT; ++i) {
            if (contains(profiles[i], x, y)) {
                *selected = i;
                return TV_MENU_PROFILE;
            }
        }
        return TV_MENU_NONE;
    }
    if (view == TV_MENU_ADJUSTMENTS) {
        const tv_menu_action_t actions[] = {TV_MENU_VOL_DOWN, TV_MENU_VOL_UP, TV_MENU_DIM, TV_MENU_BRIGHTEN};
        for (unsigned i = 0; i < sizeof adjustment / sizeof adjustment[0]; ++i) {
            if (contains(adjustment[i], x, y)) return actions[i];
        }
        const rectangle_t video = {0, TV_STATUS_Y + TV_STATUS_ROWS, TV_MENU_WIDTH,
                                   TV_MENU_BOTTOM_Y - TV_STATUS_Y - TV_STATUS_ROWS};
        return contains(video, x, y) ? TV_MENU_HIDE : TV_MENU_NONE;
    }
    if (page >= tv_menu_page_count(count)) return TV_MENU_NONE;
    if (page && contains(previous, x, y)) return TV_MENU_PREVIOUS;
    if (page + 1 < tv_menu_page_count(count) && contains(next, x, y)) return TV_MENU_NEXT;
    for (unsigned i = 0; i < TV_MENU_PAGE_ITEMS; ++i) {
        unsigned index = page * TV_MENU_PAGE_ITEMS + i;
        if (index < count && contains(tiles[i], x, y)) {
            *selected = index;
            return TV_MENU_CHANNEL;
        }
    }
    return TV_MENU_NONE;
}

static void tile(ui_surface_t surface, rectangle_t area, const char *name, bool highlighted)
{
    const uint16_t bg = highlighted ? 0x07ff : 0x2188;
    ui_text_fill(surface, area.x, area.y, area.w, area.h, bg);
    ui_text_run_t run;
    ui_text_prepare(&run, name, area.w - 24);
    ui_text_paint(surface, area.x + 12, area.y + (area.h - UI_TEXT_LINE_H) / 2, &run,
                  highlighted ? 0 : 0xffff);
}

void tv_menu_top(ui_surface_t surface, const char *title, unsigned fps, tv_menu_view_t view)
{
    char quality[24], channel[112];
    if (fps) snprintf(quality, sizeof quality, "%u FPS", fps);
    else snprintf(quality, sizeof quality, "FPS AUTO");
    const bool channels_open = view == TV_MENU_CHANNELS;
    const bool paused = view == TV_MENU_PAUSED;
    const char *label = paused ? "已暂停" : channels_open ? "收起" :
                        view == TV_MENU_STATUS ? "播放状态" : "频道";
    snprintf(channel, sizeof channel, "%s  %s", label, title);
    ui_text_fill(surface, 0, 0, TV_MENU_WIDTH, TV_MENU_TOP_ROWS, 0x0842);
    tile(surface, top_channel, channel, channels_open);
    if (!paused) {
        tile(surface, top_pause, "暂停", false);
        if (view != TV_MENU_STATUS) tile(surface, top_quality, quality, view == TV_MENU_PROFILES);
    }
}

void tv_channel_notice_confirm(tv_channel_notice_t *notice, const char *channel, const char *title)
{
    if (!*channel || !strcmp(channel, notice->channel)) return;
    snprintf(notice->channel, sizeof notice->channel, "%s", channel);
    ui_text_prepare(&notice->title, *title ? title : channel, AV_VIDEO_WIDTH - 24);
    notice->waiting_for_video = true;
}

bool tv_channel_notice_frame(tv_channel_notice_t *notice, int64_t due_us)
{
    if (notice->waiting_for_video) {
        notice->waiting_for_video = false;
        notice->until_us = due_us + TV_CHANNEL_NOTICE_US;
    }
    return notice->title.count && due_us < notice->until_us;
}

void tv_channel_notice_draw(uint16_t *pixels, const ui_text_run_t *title)
{
    const unsigned start = (AV_VIDEO_HEIGHT - TV_CHANNEL_NOTICE_ROWS) * AV_VIDEO_WIDTH;
    const ui_surface_t band = {.pixels = (uint8_t *)(pixels + start), .width = AV_VIDEO_WIDTH,
                                .rows = TV_CHANNEL_NOTICE_ROWS};
    ui_text_fill(band, 0, 0, AV_VIDEO_WIDTH, TV_CHANNEL_NOTICE_ROWS, UI_COLOR_BOX);
    ui_text_paint(band, (AV_VIDEO_WIDTH - title->width) / 2, 4, title, UI_COLOR_TEXT);
    /* Black and white use identical bytes in either RGB565 byte order. */
}

int tv_volume_notice_level(bool playback, int64_t now_us, int64_t until_us, unsigned level)
{
    return playback && now_us < until_us ? (int)level : -1;
}

void tv_volume_notice_draw(ui_surface_t surface, int level)
{
    ui_text_fill(surface, 0, TV_VOLUME_NOTICE_Y, TV_MENU_WIDTH, TV_VOLUME_NOTICE_ROWS, UI_COLOR_BOX);
    if (level < 0) return;
    char label[24];
    snprintf(label, sizeof label, "音量 %d%%", level);
    const int x = (TV_MENU_WIDTH - AV_VIDEO_WIDTH) / 2;
    ui_text_draw(surface, x + 12, TV_VOLUME_NOTICE_Y + 4, label, UI_COLOR_TEXT, UI_COLOR_BOX);
    const int bar_x = x + 136, bar_y = TV_VOLUME_NOTICE_Y + 9, bar_w = AV_VIDEO_WIDTH - 148;
    ui_text_fill(surface, bar_x, bar_y, bar_w, 6, UI_COLOR_RULE);
    ui_text_fill(surface, bar_x, bar_y, bar_w * level / 100, 6, UI_COLOR_SELECT);
}

void tv_menu_paused(ui_surface_t surface)
{
    ui_text_fill(surface, 0, TV_MENU_BOTTOM_Y, TV_MENU_WIDTH, TV_MENU_BOTTOM_ROWS, 0x0842);
    tile(surface, resume, "继续播放", true);
    tile(surface, setup, "网络与电视设置", false);
}

void tv_menu_status(ui_surface_t surface, int wifi_rssi, const char *local_ip,
                    tv_link_status_t link, tv_audio_status_t audio)
{
    static const char *const links[] = {"等待网络", "连接服务", "播放中", "正在重连"};
    static const char *const speakers[] = {"未配置", "等待同步", "已同步"};
    const uint16_t background = 0x0842;
    char field[32];
    ui_text_fill(surface, 0, TV_MENU_BOTTOM_Y, TV_MENU_WIDTH, TV_MENU_BOTTOM_ROWS, background);
    if (wifi_rssi < 0) snprintf(field, sizeof field, "Wi-Fi %d dBm", wifi_rssi);
    else snprintf(field, sizeof field, "Wi-Fi --");
    ui_text_draw(surface, 16, 350, field, UI_COLOR_TEXT, background);
    snprintf(field, sizeof field, "IP %s", local_ip);
    ui_text_draw(surface, 248, 350, field, UI_COLOR_TEXT, background);
    snprintf(field, sizeof field, "视频 %s", links[link]);
    ui_text_draw(surface, 16, 384, field, UI_COLOR_TEXT, background);
    snprintf(field, sizeof field, "音频 %s", speakers[audio]);
    ui_text_draw(surface, 248, 384, field, UI_COLOR_TEXT, background);
    tile(surface, status_back, "返回播放", true);
    tile(surface, status_setup, "网络与电视设置", false);
}

void tv_menu_profiles(ui_surface_t surface, unsigned selected)
{
    ui_text_fill(surface, 0, TV_MENU_BOTTOM_Y, TV_MENU_WIDTH, TV_MENU_BOTTOM_ROWS, 0x0842);
    for (unsigned i = 0; i < TV_PROFILE_COUNT; ++i) {
        char label[32];
        snprintf(label, sizeof label, "%s %u FPS", tv_profiles[i].label, tv_profiles[i].target_fps);
        tile(surface, profiles[i], label, selected == i);
    }
}

void tv_menu_bottom(ui_surface_t surface, bool open, unsigned page, unsigned count,
                    unsigned current, const char *const *names)
{
    ui_text_fill(surface, 0, TV_MENU_BOTTOM_Y, TV_MENU_WIDTH, TV_MENU_BOTTOM_ROWS, 0x0842);
    if (!open) return;
    if (!count) {
        tile(surface, tiles[0], "等待频道列表", false);
        return;
    }
    char label[24];
    unsigned pages = tv_menu_page_count(count);
    if (page >= pages) page = pages - 1;
    for (unsigned i = 0; i < TV_MENU_PAGE_ITEMS; ++i) {
        const unsigned index = page * TV_MENU_PAGE_ITEMS + i;
        if (index < count) tile(surface, tiles[i], names[index], index == current);
    }
    if (page) tile(surface, previous, "上一页", false);
    if (page + 1 < pages) tile(surface, next, "下一页", false);
    snprintf(label, sizeof label, "%u / %u", page + 1, pages);
    ui_text_draw(surface, 220, 447, label, UI_COLOR_TEXT, 0x0842);
}

void tv_menu_adjustments(ui_surface_t surface, unsigned volume, unsigned brightness, bool speaker)
{
    char label[32];
    ui_text_fill(surface, 0, TV_MENU_BOTTOM_Y, TV_MENU_WIDTH, TV_MENU_BOTTOM_ROWS, 0x0842);
    if (speaker) {
        tile(surface, adjustment[0], "- 10%", false);
        tile(surface, adjustment[1], "+ 10%", false);
        snprintf(label, sizeof label, "音量 %u%%", volume);
        ui_text_draw(surface, 170, 360, label, UI_COLOR_TEXT, 0x0842);
    }
    tile(surface, adjustment[2], "- 10%", false);
    tile(surface, adjustment[3], "+ 10%", false);
    snprintf(label, sizeof label, "亮度 %u%%", brightness);
    ui_text_draw(surface, 170, 425, label, UI_COLOR_TEXT, 0x0842);
}

unsigned tv_menu_level(unsigned value, unsigned minimum, bool up)
{
    if (up) return value < 90 ? value + 10 : 100;
    return value > minimum + 10 ? value - 10 : minimum;
}
