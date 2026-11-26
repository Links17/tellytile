#pragma once

#include "ui_text.h"
#include "stream_status.h"
#include "av_protocol.h"

enum { TV_MENU_WIDTH = 480, TV_MENU_TOP_ROWS = 82, TV_MENU_BOTTOM_Y = 336,
       TV_MENU_BOTTOM_ROWS = 144, TV_MENU_PAGE_ITEMS = 4 };
typedef enum { TV_MENU_ADJUSTMENTS, TV_MENU_CHANNELS, TV_MENU_PROFILES, TV_MENU_PAUSED, TV_MENU_STATUS, TV_MENU_HIDDEN } tv_menu_view_t;
enum { TV_CHANNEL_NOTICE_US = 5000000, TV_CHANNEL_NOTICE_ROWS = UI_TEXT_LINE_H + 8 };
enum { TV_VOLUME_NOTICE_US = 1500000, TV_VOLUME_NOTICE_Y = TV_STATUS_Y + TV_STATUS_ROWS,
       TV_VOLUME_NOTICE_ROWS = UI_TEXT_LINE_H + 8 };
typedef struct {
    char channel[AV_CHANNEL_ID_MAX];
    ui_text_run_t title;
    int64_t until_us;
    bool waiting_for_video;
} tv_channel_notice_t;
typedef enum {
    TV_MENU_NONE, TV_MENU_TOGGLE, TV_MENU_QUALITY, TV_MENU_PREVIOUS, TV_MENU_NEXT, TV_MENU_CHANNEL,
    TV_MENU_VOL_DOWN, TV_MENU_VOL_UP, TV_MENU_DIM, TV_MENU_BRIGHTEN,
    TV_MENU_PROFILE, TV_MENU_PAUSE, TV_MENU_RESUME, TV_MENU_SETUP,
    TV_MENU_CHANNEL_BACK, TV_MENU_CHANNEL_FORWARD, TV_MENU_TOGGLE_STATUS, TV_MENU_SHOW, TV_MENU_HIDE,
} tv_menu_action_t;

typedef struct {
    int x, y;
    int dx, dy;
    unsigned max_x, max_y;
    bool pressed, interrupted;
} tv_touch_tracker_t;

typedef enum { TV_TOUCH_TAP, TV_TOUCH_SWIPE_UP, TV_TOUCH_SWIPE_DOWN } tv_touch_kind_t;
typedef struct { int x, y; tv_touch_kind_t kind; int64_t started_us; } tv_touch_event_t;

bool tv_touch_sample(tv_touch_tracker_t *tracker, bool valid, bool contact,
                     int x, int y, tv_touch_event_t *event);
int64_t tv_menu_deadline(tv_menu_view_t view, int64_t now_us);
bool tv_menu_expired(tv_menu_view_t view, int64_t deadline_us, int64_t now_us);
unsigned tv_menu_page_count(unsigned count);
unsigned tv_menu_restore_page(unsigned page, unsigned count, unsigned current, bool visited);
tv_menu_action_t tv_menu_touch(tv_menu_view_t view, unsigned page, unsigned count,
                               tv_touch_event_t event, unsigned *selected);
bool tv_channel_move(const char *const *ids, unsigned count, char *id, size_t capacity, int direction);
tv_menu_action_t tv_menu_hit(tv_menu_view_t view, unsigned page, unsigned count, int x, int y, unsigned *selected);
void tv_menu_top(ui_surface_t surface, const char *title, unsigned fps, tv_menu_view_t view);
void tv_channel_notice_confirm(tv_channel_notice_t *notice, const char *channel, const char *title);
bool tv_channel_notice_frame(tv_channel_notice_t *notice, int64_t due_us);
void tv_channel_notice_draw(uint16_t *pixels, const ui_text_run_t *title);
int tv_volume_notice_level(bool playback, int64_t now_us, int64_t until_us, unsigned level);
void tv_volume_notice_draw(ui_surface_t surface, int level);
void tv_menu_bottom(ui_surface_t surface, bool open, unsigned page, unsigned count,
                    unsigned current, const char *const *names);
void tv_menu_adjustments(ui_surface_t surface, unsigned volume, unsigned brightness, bool speaker);
void tv_menu_paused(ui_surface_t surface);
void tv_menu_status(ui_surface_t surface, int wifi_rssi, const char *local_ip,
                    tv_link_status_t link, tv_audio_status_t audio);
void tv_menu_profiles(ui_surface_t surface, unsigned selected);
unsigned tv_menu_level(unsigned value, unsigned minimum, bool up);
