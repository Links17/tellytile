#include "av_channel_policy.h"
#include "channel_menu.h"
#include "video_fill.h"
#include "playback_profile.h"
#include "relay_volume.h"
#include "stream_status.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t pixels[TV_MENU_WIDTH * TV_MENU_BOTTOM_ROWS * 2];

static void check_channel_notice(void)
{
    tv_channel_notice_t notice = {0};
    assert(!tv_channel_notice_frame(&notice, 0));
    tv_channel_notice_confirm(&notice, "", "Waiting for CONFIG");
    assert(!tv_channel_notice_frame(&notice, 10));
    tv_channel_notice_confirm(&notice, "news", "综合新闻");
    assert(notice.waiting_for_video && notice.until_us == 0);
    tv_channel_notice_confirm(&notice, "news", "综合新闻"); /* Same channel reconnected before video. */
    assert(tv_channel_notice_frame(&notice, 10000));
    assert(notice.until_us == 10000 + TV_CHANNEL_NOTICE_US);
    tv_channel_notice_confirm(&notice, "news", "综合新闻");
    assert(tv_channel_notice_frame(&notice, notice.until_us - 1));
    assert(!tv_channel_notice_frame(&notice, notice.until_us));
    tv_channel_notice_confirm(&notice, "news", "综合新闻");
    assert(!tv_channel_notice_frame(&notice, notice.until_us + 1));
    tv_channel_notice_confirm(&notice, "sports", "综合新闻");
    assert(tv_channel_notice_frame(&notice, 7000000)); /* Distinct stations can share a title. */
    assert(notice.until_us == 12000000);

    static uint16_t frame[AV_VIDEO_WIDTH * AV_VIDEO_HEIGHT];
    for (unsigned i = 0; i < AV_VIDEO_WIDTH * AV_VIDEO_HEIGHT; ++i) frame[i] = 0xf800;
    tv_channel_notice_draw(frame, &notice.title);
    const unsigned first = (AV_VIDEO_HEIGHT - TV_CHANNEL_NOTICE_ROWS) * AV_VIDEO_WIDTH;
    const unsigned left = (AV_VIDEO_WIDTH - notice.title.width) / 2;
    for (unsigned i = 0; i < first; ++i) assert(frame[i] == 0xf800);
    unsigned glyph_pixels = 0;
    for (unsigned i = first; i < AV_VIDEO_WIDTH * AV_VIDEO_HEIGHT; ++i) {
        assert(frame[i] == 0 || frame[i] == UINT16_MAX);
        if (frame[i] == UINT16_MAX) {
            assert(i % AV_VIDEO_WIDTH >= left && i % AV_VIDEO_WIDTH < left + notice.title.width);
            ++glyph_pixels;
        }
    }
    assert(glyph_pixels > 30);
    puts("Confirmed channel caption starts with video, expires at five seconds, ignores same-station reconnect and changes only its lower video band.");
}

static void check_volume_notice(void)
{
    const int64_t start = 140000, expires = start + TV_VOLUME_NOTICE_US;
    assert(tv_volume_notice_level(true, start, expires, 60) == 60);
    assert(tv_volume_notice_level(true, expires - 1, expires, 60) == 60);
    assert(tv_volume_notice_level(true, expires, expires, 60) == -1);
    assert(tv_volume_notice_level(false, start, expires, 60) == -1);
    assert(tv_volume_notice_level(true, start, expires, 0) == 0);

    uint8_t buffer[(TV_VOLUME_NOTICE_ROWS + 2) * TV_MENU_WIDTH * 2];
    memset(buffer, 0xa5, sizeof buffer);
    const ui_surface_t band = {.pixels = buffer, .width = TV_MENU_WIDTH,
                               .origin_y = TV_VOLUME_NOTICE_Y - 1, .rows = TV_VOLUME_NOTICE_ROWS + 2};
    const unsigned start_x = 216, end_x = 388;
    uint8_t *bar = buffer + 10 * TV_MENU_WIDTH * 2;
    const unsigned levels[] = {0, 60, 100};
    for (unsigned sample = 0; sample < sizeof levels / sizeof levels[0]; ++sample) {
        tv_volume_notice_draw(band, levels[sample]);
        const unsigned fill_end = start_x + (end_x - start_x) * levels[sample] / 100;
        for (unsigned x = start_x; x < end_x; ++x) {
            const unsigned color = x < fill_end ? UI_COLOR_SELECT : UI_COLOR_RULE;
            assert(bar[2 * x] == color >> 8 && bar[2 * x + 1] == (color & 255));
        }
        bool text = false;
        for (unsigned y = 5; y < 21; ++y)
            for (unsigned x = 92; x < start_x; ++x)
                text |= buffer[(y * TV_MENU_WIDTH + x) * 2] == 255;
        assert(text);
    }
    tv_volume_notice_draw(band, -1);
    for (size_t i = 0; i < sizeof buffer; ++i)
        assert(buffer[i] == (i < TV_MENU_WIDTH * 2 || i >= sizeof buffer - TV_MENU_WIDTH * 2 ? 0xa5 : 0));
    puts("Volume notice shows text and the correct zero, partial and full bar, expires at 1.5 seconds, and leaves adjacent bands intact.");
}

static void check_touch_channels(void)
{
    tv_touch_tracker_t tracker = {0};
    tv_touch_event_t event;
    unsigned selected = 0;
    assert(!tv_touch_sample(&tracker, true, true, 180, 250, &event));
    assert(!tv_touch_sample(&tracker, true, true, 190, 194, &event));
    assert(tv_touch_sample(&tracker, true, false, 0, 0, &event));
    assert(event.kind == TV_TOUCH_SWIPE_UP);
    assert(tv_menu_touch(TV_MENU_ADJUSTMENTS, 0, 4, event, &selected) == TV_MENU_CHANNEL_FORWARD);
    assert(tv_menu_touch(TV_MENU_CHANNELS, 0, 4, event, &selected) == TV_MENU_NONE);
    assert(tv_menu_touch(TV_MENU_PROFILES, 0, 4, event, &selected) == TV_MENU_NONE);
    assert(tv_menu_touch(TV_MENU_PAUSED, 0, 4, event, &selected) == TV_MENU_NONE);
    assert(tv_menu_touch(TV_MENU_STATUS, 0, 4, event, &selected) == TV_MENU_NONE);
    assert(tv_menu_touch(TV_MENU_ADJUSTMENTS, 0, 1, event, &selected) == TV_MENU_NONE);

    assert(!tv_touch_sample(&tracker, true, true, 180, 195, &event));
    assert(!tv_touch_sample(&tracker, true, true, 205, 275, &event));
    assert(tv_touch_sample(&tracker, true, false, 0, 0, &event));
    assert(tv_menu_touch(TV_MENU_ADJUSTMENTS, 0, 4, event, &selected) == TV_MENU_CHANNEL_BACK);
    event.y = 80;
    assert(tv_menu_touch(TV_MENU_ADJUSTMENTS, 0, 4, event, &selected) == TV_MENU_NONE);

    assert(!tv_touch_sample(&tracker, true, true, 20, 25, &event));
    assert(!tv_touch_sample(&tracker, true, true, 24, 28, &event));
    assert(tv_touch_sample(&tracker, true, false, 0, 0, &event));
    assert(tv_menu_touch(TV_MENU_ADJUSTMENTS, 0, 4, event, &selected) == TV_MENU_TOGGLE);

    assert(!tv_touch_sample(&tracker, true, true, 180, 250, &event));
    assert(!tv_touch_sample(&tracker, true, true, 240, 194, &event));
    assert(!tv_touch_sample(&tracker, true, true, 182, 252, &event));
    assert(!tv_touch_sample(&tracker, true, false, 0, 0, &event));

    assert(!tv_touch_sample(&tracker, true, true, 180, 250, &event));
    assert(!tv_touch_sample(&tracker, false, true, 200, 200, &event));
    assert(!tv_touch_sample(&tracker, true, true, 180, 180, &event));
    assert(!tv_touch_sample(&tracker, true, false, 0, 0, &event));

    const char *ids[] = {"a", "b", "c"};
    char channel[16] = "a";
    assert(tv_channel_move(ids, 3, channel, sizeof channel, -1) && !strcmp(channel, "c"));
    assert(tv_channel_move(ids, 3, channel, sizeof channel, 1) && !strcmp(channel, "a"));
    assert(tv_channel_move(ids, 3, channel, sizeof channel, 1) && !strcmp(channel, "b"));
    assert(!tv_channel_move(ids, 0, channel, sizeof channel, -1));
    const char *single[] = {"b"};
    assert(!tv_channel_move(single, 1, channel, sizeof channel, 1) && !strcmp(channel, "b"));
    assert(!tv_channel_move(single, 1, channel, sizeof channel, -1) && !strcmp(channel, "b"));
    channel[0] = '\0';
    assert(tv_channel_move(single, 1, channel, sizeof channel, 1) && !strcmp(channel, "b"));
    const char *invalid[] = {""};
    assert(!tv_channel_move(invalid, 1, channel, sizeof channel, 1) && !strcmp(channel, "b"));
    assert(tv_menu_restore_page(0, 12, 5, false) == 1);
    assert(tv_menu_restore_page(2, 12, 5, true) == 2);
    assert(tv_menu_restore_page(2, 8, 5, true) == 1);
    puts("Playback gestures passed: directional wrap, tap, interrupted and diagonal contact, modal isolation, and list-page retention.");
}

static void check_channel_recovery(void)
{
    av_channel_policy_t policy = {0};
    unsigned streak = 0;
    for (unsigned i = 0; i < 2; ++i)
        assert(!av_channel_policy_should_skip(&policy, "auto", true, false, &streak, 3, 3));
    assert(av_channel_policy_should_skip(&policy, "auto", true, false, &streak, 3, 3));
    for (unsigned i = 0; i < 4; ++i)
        assert(!av_channel_policy_should_skip(&policy, "chosen", true, false, &streak, 5, 3));
    assert(av_channel_policy_should_skip(&policy, "chosen", true, false, &streak, 5, 3));

    assert(!av_channel_policy_should_skip(&policy, "chosen", true, true, &streak, 5, 3));
    assert(av_channel_policy_has_shown(&policy, "chosen"));
    assert(!av_channel_policy_should_skip(&policy, "other", true, true, &streak, 3, 3));
    for (unsigned i = 0; i < 8; ++i) {
        assert(!av_channel_policy_should_skip(&policy, "chosen", true, false, &streak, 3, 3));
        assert(!av_channel_policy_should_skip(&policy, "new", false, false, &streak, 3, 3));
        assert(!av_channel_policy_should_skip(&policy, "only", true, false, &streak, 3, 1));
    }
    assert(!av_channel_policy_should_skip(&policy, "new", true, false, &streak, 3, 3));
    assert(!av_channel_policy_should_skip(&policy, "new", true, false, &streak, 3, 3));
    assert(streak == 2);
    assert(!av_channel_policy_should_skip(&policy, "new", false, false, &streak, 3, 3));
    assert(streak == 0);
    puts("Channel recovery kept a proven channel and reset offline failures; unproven channels advanced after three automatic or five deliberate attempts.");
}

static bool has_foreground(int x, int y, int w, int h, uint16_t color)
{
    for (int row = y; row < y + h; ++row)
        for (int column = x; column < x + w; ++column) {
            size_t i = 2 * (row * TV_MENU_WIDTH + column);
            if (((unsigned)pixels[i] << 8 | pixels[i + 1]) == color) return true;
        }
    return false;
}

static void check_playback_profiles(void)
{
    const int column[] = {35, 190, 340};
    for (unsigned i = 0; i < TV_PROFILE_COUNT; ++i) {
        unsigned selected;
        assert(tv_menu_hit(TV_MENU_PROFILES, 0, 0, column[i], 365, &selected) == TV_MENU_PROFILE);
        assert(selected == i && tv_profile_restore(i) == i);
    }
    unsigned selected;
    assert(tv_menu_hit(TV_MENU_PROFILES, 0, 0, 162, 365, &selected) == TV_MENU_NONE);
    assert(tv_profile_restore(TV_PROFILE_COUNT) == TV_ULTRA);
    assert(tv_profile_changes(TV_STANDARD, TV_ULTRA, true));
    assert(!tv_profile_changes(TV_STANDARD, TV_STANDARD, true));
    assert(!tv_profile_changes(TV_STANDARD, TV_ULTRA, false));
    assert(!tv_profile_changes(TV_STANDARD, TV_PROFILE_COUNT, true));
    assert(tv_profiles[TV_SMOOTH].target_fps == 8);
    assert(tv_profiles[TV_STANDARD].target_fps == 15);
    assert(tv_profiles[TV_ULTRA].target_fps == 30);

    memset(pixels, 0xcc, sizeof pixels);
    const ui_surface_t band = {.pixels = pixels, .width = TV_MENU_WIDTH,
                               .origin_y = TV_MENU_BOTTOM_Y, .rows = TV_MENU_BOTTOM_ROWS};
    tv_menu_profiles(band, TV_STANDARD);
    assert(has_foreground(28, 22, 115, 66, 0xffff));
    assert(has_foreground(182, 22, 115, 66, 0));
    assert(has_foreground(336, 22, 115, 66, 0xffff));

    cJSON *config = cJSON_Parse(
        "{\"session\":9,\"width\":320,\"height\":180,\"stripe_rows\":12,"
        "\"video_max_bytes\":22528,\"sample_rate\":16000,\"sample_bits\":16,\"channels\":1,"
        "\"audio_chunk_ms\":40,\"audio_codec\":\"ima_adpcm\",\"quality\":\"standard\","
        "\"start_delay_ms\":200,\"fps\":12}");
    bool supports_profiles = false;
    assert(config && tv_session_config_valid(config, 9, TV_STANDARD, &supports_profiles) && supports_profiles);
    assert(!tv_session_config_valid(config, 10, TV_STANDARD, &supports_profiles));
    assert(!tv_session_config_valid(config, 9, TV_ULTRA, &supports_profiles));
    cJSON *fps = cJSON_GetObjectItemCaseSensitive(config, "fps");
    cJSON_SetNumberValue(fps, 1);
    assert(tv_session_config_valid(config, 9, TV_STANDARD, &supports_profiles));
    cJSON_SetNumberValue(fps, 30);
    assert(tv_session_config_valid(config, 9, TV_STANDARD, &supports_profiles));
    const double invalid_fps[] = {0, 31, 8.5};
    for (unsigned i = 0; i < sizeof invalid_fps / sizeof invalid_fps[0]; ++i) {
        cJSON_SetNumberValue(fps, invalid_fps[i]);
        assert(!tv_session_config_valid(config, 9, TV_STANDARD, &supports_profiles));
    }
    cJSON_SetNumberValue(fps, 15);
    cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(config, "start_delay_ms"), 201);
    assert(!tv_session_config_valid(config, 9, TV_STANDARD, &supports_profiles));
    cJSON_DeleteItemFromObjectCaseSensitive(config, "quality");
    cJSON_DeleteItemFromObjectCaseSensitive(config, "start_delay_ms");
    assert(tv_session_config_valid(config, 9, TV_ULTRA, &supports_profiles) && !supports_profiles);
    cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(config, "stripe_rows"), 10);
    assert(!tv_session_config_valid(config, 9, TV_ULTRA, &supports_profiles));
    cJSON_Delete(config);
    puts("Quality cards, saved profile decoding, exact session geometry, FPS limits and legacy CONFIG passed.");
}

static void check_volume_wire(void)
{
    const relay_volume_t set = {.level = 70, .generation = 0x01020304, .revision = 0x50607080};
    uint8_t wire[RELAY_VOLUME_BYTES + 1];
    relay_volume_t decoded;
    relay_volume_encode(wire, RELAY_VOLUME_SET, set);
    const uint8_t expected[] = {'F', 'T', 'V', '1', 1, 70, 0, 0, 1, 2, 3, 4, 0x50, 0x60, 0x70, 0x80};
    assert(!memcmp(wire, expected, sizeof expected));
    assert(!relay_volume_decode(wire, sizeof wire, RELAY_VOLUME_SET, &decoded));
    assert(!relay_volume_decode(wire, RELAY_VOLUME_BYTES, RELAY_VOLUME_ACK, &decoded));
    assert(relay_volume_decode(wire, RELAY_VOLUME_BYTES, RELAY_VOLUME_SET, &decoded));
    assert(relay_volume_matches(decoded, set) && !relay_volume_newer(decoded, set));
    relay_volume_t newer = decoded;
    newer.level = 0;
    ++newer.revision;
    assert(relay_volume_newer(newer, set) && !relay_volume_newer(set, newer));
    relay_volume_encode(wire, RELAY_VOLUME_ACK, newer);
    assert(relay_volume_decode(wire, RELAY_VOLUME_BYTES, RELAY_VOLUME_ACK, &decoded));
    assert(relay_volume_matches(newer, decoded) && !relay_volume_matches(set, decoded));
    wire[5] = 101;
    assert(!relay_volume_decode(wire, RELAY_VOLUME_BYTES, RELAY_VOLUME_ACK, &decoded));
    newer.generation++;
    newer.revision = 1;
    assert(relay_volume_newer(newer, set));
}

static void check_stream_status(void)
{
    tv_status_t status = {0};
    const tv_stream_totals_t baseline = {UINT32_MAX - 9, UINT32_MAX - 1023};
    tv_status_reset(&status, 250000, baseline);
    assert(!tv_status_sample(&status, 1249999, (tv_stream_totals_t){20, 64512}, true));
    assert(tv_status_sample(&status, 2250000, (tv_stream_totals_t){20, 64512}, true));
    assert(status.reading.fps_tenths == 150 && status.reading.kib_per_second == 32);
    tv_status_reset(&status, 2500000, (tv_stream_totals_t){20, 64512});
    assert(!status.reading.fps_tenths && !status.reading.kib_per_second);
    assert(tv_status_sample(&status, 3500000, (tv_stream_totals_t){100, 110000}, false));
    assert(!status.reading.fps_tenths && !status.reading.kib_per_second);
    assert(tv_wifi_bars(0) == 0 && tv_wifi_bars(-90) == 1 && tv_wifi_bars(-76) == 1);
    assert(tv_wifi_bars(-75) == 2 && tv_wifi_bars(-66) == 2 && tv_wifi_bars(-65) == 3);
    assert(tv_wifi_bars(-56) == 3 && tv_wifi_bars(-55) == 4);

    memset(pixels, 0xcc, sizeof pixels);
    const ui_surface_t band = {.pixels = pixels, .width = TV_MENU_WIDTH,
                               .origin_y = TV_STATUS_Y, .rows = TV_STATUS_ROWS};
    const tv_status_reading_t reading = {.wifi_bars = 4, .fps_tenths = 999, .kib_per_second = 9999};
    tv_status_draw(band, reading);
    const int power_x = TV_MENU_WIDTH - 16 - ui_text_width("USB 5V");
    assert(252 + ui_text_width("RX 9999 KiB/s") < power_x);
    assert(has_foreground(16, 0, 113, TV_STATUS_ROWS, UI_COLOR_TEXT));
    assert(has_foreground(129, 0, 123, TV_STATUS_ROWS, UI_COLOR_TEXT));
    assert(has_foreground(252, 0, power_x - 252, TV_STATUS_ROWS, UI_COLOR_TEXT));
    assert(has_foreground(power_x, 0, TV_MENU_WIDTH - power_x, TV_STATUS_ROWS, UI_COLOR_TEXT));
    assert(pixels[TV_MENU_WIDTH * TV_STATUS_ROWS * 2] == 0xcc);
    unsigned channel;
    assert(tv_menu_hit(TV_MENU_ADJUSTMENTS, 0, 5, power_x, TV_STATUS_Y + 10, &channel) == TV_MENU_TOGGLE_STATUS);
    assert(tv_menu_hit(TV_MENU_PAUSED, 0, 5, power_x, TV_STATUS_Y + 10, &channel) == TV_MENU_NONE);
    puts("Status bar passed RSSI thresholds, displayed-FPS and ingress sampling, counter wrap, "
         "reset, USB power label and band boundaries.");
}

static void check_status_detail(void)
{
    unsigned channel;
    assert(tv_menu_hit(TV_MENU_STATUS, 0, 5, 16, 440, &channel) == TV_MENU_TOGGLE_STATUS);
    assert(tv_menu_hit(TV_MENU_STATUS, 0, 5, 248, 440, &channel) == TV_MENU_SETUP);
    assert(tv_menu_hit(TV_MENU_STATUS, 0, 5, 340, 335, &channel) == TV_MENU_NONE);
    assert(tv_menu_hit(TV_MENU_STATUS, 0, 5, 250, 441, &channel) != TV_MENU_BRIGHTEN);
    assert(tv_menu_hit(TV_MENU_STATUS, 0, 5, 276, 44, &channel) == TV_MENU_PAUSE);

    assert(tv_status_link(false, true, false, 150) == TV_LINK_WAITING);
    assert(tv_status_link(true, true, true, 150) == TV_LINK_RECOVERING);
    assert(tv_status_link(true, true, false, 0) == TV_LINK_CONNECTING);
    assert(tv_status_link(true, true, false, 150) == TV_LINK_PLAYING);
    assert(tv_status_audio(false, 1, 1000, 700) == TV_AUDIO_DISABLED);
    assert(tv_status_audio(true, 0, 1000, 700) == TV_AUDIO_WAITING);
    assert(tv_status_audio(true, 1, 1000, 0) == TV_AUDIO_WAITING);
    assert(tv_status_audio(true, 1, 1000, 2000) == TV_AUDIO_WAITING);
    assert(tv_status_audio(true, 1, 3001000, 1000) == TV_AUDIO_SYNCED);
    assert(tv_status_audio(true, 1, 3001001, 1000) == TV_AUDIO_WAITING);

    const ui_surface_t band = {.pixels = pixels, .width = TV_MENU_WIDTH,
                               .origin_y = TV_MENU_BOTTOM_Y, .rows = TV_MENU_BOTTOM_ROWS};
    tv_menu_status(band, -77, "192.168.100.104", TV_LINK_PLAYING, TV_AUDIO_SYNCED);
    assert(has_foreground(16, 14, 216, 16, UI_COLOR_TEXT));
    assert(has_foreground(248, 14, 216, 16, UI_COLOR_TEXT));
    assert(has_foreground(16, 48, 216, 16, UI_COLOR_TEXT));
    assert(has_foreground(248, 48, 216, 16, UI_COLOR_TEXT));
    assert(has_foreground(28, 92, 192, 36, 0));
    assert(has_foreground(260, 92, 192, 36, 0xffff));
    puts("Detailed status rendered separate RSSI, IP, video and audio fields, preserved the video band, and expired stale speaker sync.");
}

static void check_menu_idle(void)
{
    const int64_t opened = 44000000;
    const int64_t status = tv_menu_deadline(TV_MENU_STATUS, opened);
    const int64_t channels = tv_menu_deadline(TV_MENU_CHANNELS, opened);
    assert(status == opened + 8000000);
    assert(channels == opened + 12000000);
    assert(tv_menu_deadline(TV_MENU_PROFILES, opened) == channels);
    const int64_t controls = tv_menu_deadline(TV_MENU_ADJUSTMENTS, opened);
    assert(controls == opened + 6000000);
    assert(tv_menu_deadline(TV_MENU_HIDDEN, opened) == 0);
    assert(tv_menu_deadline(TV_MENU_PAUSED, opened) == 0);
    assert(!tv_menu_expired(TV_MENU_STATUS, status, status - 1));
    assert(tv_menu_expired(TV_MENU_STATUS, status, status));

    const int64_t after_page_turn = tv_menu_deadline(TV_MENU_CHANNELS, channels - 1);
    assert(!tv_menu_expired(TV_MENU_CHANNELS, after_page_turn, channels));
    assert(tv_menu_expired(TV_MENU_CHANNELS, after_page_turn, after_page_turn));
    assert(!tv_menu_expired(TV_MENU_PAUSED, channels, channels + 50000000));
    assert(!tv_menu_expired(TV_MENU_HIDDEN, channels, channels + 50000000));
    assert(!tv_menu_expired(TV_MENU_ADJUSTMENTS, controls, controls - 1));
    assert(tv_menu_expired(TV_MENU_ADJUSTMENTS, controls, controls));
    puts("Menu timeout passed the 6, 8 and 12 second boundaries, page-turn extension and pause exemption.");
}

static void check_fullscreen(void)
{
    enum { WIDTH = TV_MENU_WIDTH, HEIGHT = TV_MENU_BOTTOM_Y + TV_MENU_BOTTOM_ROWS };
    static uint16_t input[AV_VIDEO_WIDTH * AV_VIDEO_HEIGHT];
    static uint16_t output[WIDTH * HEIGHT];
    uint16_t row[WIDTH];
    for (unsigned y = 0; y < AV_VIDEO_HEIGHT; ++y)
        for (unsigned x = 0; x < AV_VIDEO_WIDTH; ++x)
            input[y * AV_VIDEO_WIDTH + x] = (uint16_t)(y * AV_VIDEO_WIDTH + x + 1);
    tv_fill_video_rows(output, input, row, 124, 336);
    assert(output[123 * WIDTH] == 0 && output[336 * WIDTH] == 0);
    assert(output[124 * WIDTH] != 0 && output[336 * WIDTH - 1] != 0);
    tv_fill_video_rows(output, input, row, 0, HEIGHT);
    assert(output[0] == input[70] && output[WIDTH - 1] == input[249]);
    assert(output[HEIGHT * WIDTH - 1] == input[(AV_VIDEO_HEIGHT - 1) * AV_VIDEO_WIDTH + 249]);
    unsigned selected = 0;
    assert(tv_menu_hit(TV_MENU_HIDDEN, 0, 2, 270, 20, &selected) == TV_MENU_SHOW);
    assert(tv_menu_hit(TV_MENU_ADJUSTMENTS, 0, 2, 270, 20, &selected) == TV_MENU_PAUSE);
    assert(tv_menu_hit(TV_MENU_ADJUSTMENTS, 0, 2, 240, 240, &selected) == TV_MENU_HIDE);
    const tv_touch_event_t swipe = {.x = 2, .y = 470, .kind = TV_TOUCH_SWIPE_UP};
    assert(tv_menu_touch(TV_MENU_HIDDEN, 0, 2, swipe, &selected) == TV_MENU_CHANNEL_FORWARD);
    puts("Fullscreen filled the expected center crop, preserved overlay rows and gated the first tap.");
}

int main(void)
{
    check_fullscreen();
    check_menu_idle();
    unsigned channel;
    assert(tv_menu_page_count(0) == 0 && tv_menu_page_count(4) == 1 && tv_menu_page_count(5) == 2);
    assert(tv_menu_hit(TV_MENU_ADJUSTMENTS, 0, 5, 256, 20, &channel) == TV_MENU_PAUSE);
    assert(tv_menu_hit(TV_MENU_CHANNELS, 0, 5, 335, 20, &channel) == TV_MENU_PAUSE);
    assert(tv_menu_hit(TV_MENU_PROFILES, 0, 5, 336, 20, &channel) == TV_MENU_NONE);
    assert(tv_menu_hit(TV_MENU_PAUSED, 0, 5, 25, 20, &channel) == TV_MENU_NONE);
    assert(tv_menu_hit(TV_MENU_PAUSED, 0, 5, 25, 365, &channel) == TV_MENU_RESUME);
    assert(tv_menu_hit(TV_MENU_PAUSED, 0, 5, 240, 365, &channel) == TV_MENU_NONE);
    assert(tv_menu_hit(TV_MENU_PAUSED, 0, 5, 250, 365, &channel) == TV_MENU_SETUP);
    assert(tv_menu_hit(false, 0, 5, 25, 20, &channel) == TV_MENU_TOGGLE);
    assert(tv_menu_hit(false, 0, 5, 360, 20, &channel) == TV_MENU_QUALITY);
    assert(tv_menu_hit(false, 0, 5, 30, 351, &channel) == TV_MENU_VOL_DOWN);
    assert(tv_menu_hit(false, 0, 5, 370, 353, &channel) == TV_MENU_VOL_UP);
    assert(tv_menu_hit(false, 0, 5, 30, 418, &channel) == TV_MENU_DIM);
    assert(tv_menu_hit(false, 0, 5, 370, 418, &channel) == TV_MENU_BRIGHTEN);
    assert(tv_menu_hit(false, 0, 5, 240, 418, &channel) == TV_MENU_NONE);
    assert(tv_menu_hit(true, 0, 0, 30, 351, &channel) == TV_MENU_NONE);
    assert(tv_menu_hit(true, 0, 5, 30, 351, &channel) == TV_MENU_CHANNEL && channel == 0);
    assert(tv_menu_hit(true, 0, 5, 253, 402, &channel) == TV_MENU_CHANNEL && channel == 3);
    assert(tv_menu_hit(true, 0, 5, 335, 450, &channel) == TV_MENU_NEXT);
    assert(tv_menu_hit(true, 1, 5, 30, 351, &channel) == TV_MENU_CHANNEL && channel == 4);
    assert(tv_menu_hit(true, 1, 5, 249, 351, &channel) == TV_MENU_NONE);
    assert(tv_menu_hit(true, 1, 5, 335, 450, &channel) == TV_MENU_NONE);
    assert(tv_menu_hit(true, 1, 5, 30, 450, &channel) == TV_MENU_PREVIOUS);
    assert(tv_menu_hit(true, 0, 5, 220, 220, &channel) == TV_MENU_NONE);
    assert(tv_menu_hit(true, 0, 5, 475, 500, &channel) == TV_MENU_NONE);

    static const char *const names[] = {"测试频道", "新闻直播", "音乐频道", "纪录片", "硬件演示"};
    ui_surface_t band = {.pixels = pixels, .width = TV_MENU_WIDTH, .rows = TV_MENU_TOP_ROWS};
    memset(pixels, 0xcc, sizeof pixels);
    tv_menu_top(band, names[1], 15, false);
    assert(has_foreground(28, 32, 120, 16, 0xffff));
    assert(has_foreground(364, 32, 70, 16, 0xffff));
    assert(has_foreground(268, 32, 56, 16, 0xffff));
    tv_menu_top(band, names[1], 15, TV_MENU_PAUSED);
    assert(!has_foreground(268, 32, 56, 16, 0xffff));
    assert(pixels[TV_MENU_WIDTH * TV_MENU_TOP_ROWS * 2] == 0xcc);
    band.origin_y = TV_MENU_BOTTOM_Y;
    band.rows = TV_MENU_BOTTOM_ROWS;
    tv_menu_bottom(band, true, 1, 5, 4, names);
    assert(has_foreground(28, 18, 96, 16, 0));
    assert(!has_foreground(260, 18, 96, 16, 0xffff)); /* Empty tiles stay inactive. */
    tv_menu_bottom(band, false, 1, 5, 4, names);
    assert(!has_foreground(28, 18, 96, 16, 0));
    tv_menu_paused(band);
    assert(has_foreground(28, 44, 96, 24, 0));
    assert(has_foreground(260, 44, 160, 24, 0xffff));
    puts("Playback controls passed pause, resume and setup tap regions and labels.");
    tv_menu_adjustments(band, 60, 80, true);
    assert(has_foreground(170, 24, 80, 16, UI_COLOR_TEXT));
    assert(has_foreground(170, 89, 80, 16, UI_COLOR_TEXT));
    tv_menu_adjustments(band, 60, 80, false);
    assert(!has_foreground(170, 24, 80, 16, UI_COLOR_TEXT));
    assert(tv_menu_level(0, 0, false) == 0 && tv_menu_level(10, 10, false) == 10);
    assert(tv_menu_level(100, 0, true) == 100 && tv_menu_level(80, 10, true) == 90);
    assert(tv_menu_level(10, 0, false) == 0);
    puts("Native channel menu passed tap regions, empty and partial pages, Chinese labels and untouched video.");
    check_volume_wire();
    puts("Volume and brightness touch controls, 10% bounds, relay acknowledgements and stale revision checks passed.");
    check_stream_status();
    check_status_detail();
    check_playback_profiles();
    check_channel_recovery();
    check_touch_channels();
    check_channel_notice();
    check_volume_notice();
}
