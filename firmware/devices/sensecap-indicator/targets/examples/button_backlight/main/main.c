#include "relay_link.h"
#include "av_channel_policy.h"
#include "av_picture.h"
#include "av_protocol.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_pins.h"
#include "bsp_touch.h"
#include "cJSON.h"
#include "channel_menu.h"
#include "video_fill.h"
#include "driver/ledc.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "lwip/netdb.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "miniz.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "playback_profile.h"
#include "passport_tv.h"
#include "reconnect_view.h"
#include "sdkconfig.h"
#include "relay_volume.h"
#include "session_connection.h"
#include "setup_portal.h"
#include "setup_font.h"
#include "stream_status.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "video_clock.h"

#include <stdatomic.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#define FRAME_PIXELS (AV_VIDEO_WIDTH * AV_VIDEO_HEIGHT)
#define PACKET_CAP ((AV_VIDEO_MAX > AV_CONTROL_MAX ? AV_VIDEO_MAX : AV_CONTROL_MAX) + 1u)

static const char *TAG = "passport_tv";
static EventGroupHandle_t network;
static esp_netif_t *wifi_station;
static atomic_uint network_link;
static unsigned session_link;
typedef enum {
    TV_EVENT_NONE, TV_EVENT_NEXT, TV_EVENT_PREVIOUS, TV_EVENT_CYCLE_PROFILE, TV_EVENT_BRIGHTNESS,
    TV_EVENT_SELECT_CHANNEL, TV_EVENT_SELECT_PROFILE, TV_EVENT_PAUSE, TV_EVENT_RESUME,
    TV_EVENT_SETUP,
} tv_button_event_t;
static atomic_uint button_event;
static TickType_t pressed_at;
static char channel[AV_CHANNEL_ID_MAX];
static char channel_storage[AV_CHANNEL_MAX][AV_CHANNEL_ID_MAX];
static char channel_names[AV_CHANNEL_MAX][96], selected_channel[AV_CHANNEL_ID_MAX];
static const char *channel_ids[AV_CHANNEL_MAX];
static const char *channel_titles[AV_CHANNEL_MAX];
static unsigned channel_count, quality = TV_ULTRA, selected_quality;
static av_channel_policy_t channel_history;
static unsigned channel_empty_streak;
static bool channel_chosen_by_user;
static bool quality_available = true;
static unsigned menu_page;
static bool menu_page_visited;
static tv_menu_view_t menu_view = TV_MENU_HIDDEN;
static int64_t menu_deadline_us, menu_closed_us;
static tv_channel_notice_t channel_notice;
static atomic_bool channel_notice_visible, controls_visible, volume_notice_visible;
static tv_connection_t connection;
static char origin[TV_ORIGIN_CAP];
static char pairing_code[5], audio_peer[16];
static uint16_t audio_port;
static int audio_socket = -1;
static unsigned brightness = 80;
static relay_volume_t sound = {.revision = 1, .level = 60};
static int64_t next_volume_send, save_controls_due;
static int64_t volume_notice_until_us;
static int volume_notice_painted = -1;
static video_clock_t video_clock;
static atomic_int clock_correction_us;
static uint32_t clock_session, clock_token;
static int64_t speaker_synced_us;
static int64_t clock_probe_us, next_clock_probe;
static uint8_t *packet, *frame;
static uint16_t *rgb, *lcd_pixels;
static uint16_t scanline[TV_MENU_WIDTH];
static esp_lcd_panel_handle_t panel;
static tinfl_decompressor *inflater;
/* 12 slots cover 400 ms of decoded 30 fps video in PSRAM. */
enum { VIDEO_SLOTS = 12 };
typedef struct {
    uint16_t *pixels;
    int64_t due_us;
    int64_t notice_until_us;
    ui_text_run_t notice_title;
    unsigned generation;
} display_frame_t;
static QueueHandle_t video_free, video_ready;
static atomic_uint display_view;
static SemaphoreHandle_t panel_lock;
static QueueHandle_t touch_events;
static uint8_t *control_pixels;
static atomic_uint displayed_frames;
static uint32_t received_bytes;
static bool stream_active;
static tv_status_t stream_status;

_Static_assert(BSP_LCD_H_RES == TV_MENU_WIDTH &&
               BSP_LCD_V_RES == TV_MENU_BOTTOM_Y + TV_MENU_BOTTOM_ROWS,
               "Channel controls use the native Indicator panel geometry");
_Static_assert((int)TV_STATUS_Y >= (int)TV_MENU_TOP_ROWS &&
               TV_VOLUME_NOTICE_Y + TV_VOLUME_NOTICE_ROWS <= (BSP_LCD_V_RES - AV_VIDEO_HEIGHT) / 2,
               "Status and volume notice must stay above the video");
_Static_assert(AV_CHANNEL_POLICY_ID_MAX == AV_CHANNEL_ID_MAX, "Channel IDs must fit the retry history");

static void button(bool pressed, void *arg)
{
    (void)arg;
    if (pressed) {
        pressed_at = xTaskGetTickCount();
    } else {
        TickType_t held = xTaskGetTickCount() - pressed_at;
        tv_button_event_t kind = TV_EVENT_NEXT;
        if (held >= pdMS_TO_TICKS(3000)) kind = TV_EVENT_PAUSE;
        else if (held >= pdMS_TO_TICKS(1600)) kind = TV_EVENT_BRIGHTNESS;
        else if (held >= pdMS_TO_TICKS(600)) kind = TV_EVENT_CYCLE_PROFILE;
        atomic_store(&button_event, kind);
    }
}

static void network_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;

    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = data;
        if (!(atomic_load(&network_link) & 1u) || event->ip_changed)
            relay_link_publish(&network_link, true);
        xEventGroupSetBits(network, BIT0);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_LOST_IP) {
        relay_link_publish(&network_link, false);
        xEventGroupClearBits(network, BIT0);
    } else if (base == WIFI_EVENT && (id == WIFI_EVENT_STA_START || id == WIFI_EVENT_STA_DISCONNECTED)) {
        relay_link_publish(&network_link, false);
        xEventGroupClearBits(network, BIT0);
        esp_wifi_connect();
    }
}

static void start_wifi(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_station = esp_netif_create_default_wifi_sta();
    configASSERT(wifi_station != NULL);
    network = xEventGroupCreate();
    configASSERT(network);
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, network_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, network_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_LOST_IP, network_event, NULL));
    wifi_config_t config = {0};
    const size_t ssid_size = strlen(connection.ssid);
    const size_t password_size = strlen(connection.password);
    if (ssid_size == 0 || ssid_size > sizeof config.sta.ssid || password_size > sizeof config.sta.password) {
        ESP_LOGE(TAG, "Set valid Wi-Fi credentials in the local build configuration");
        return;
    }
    memcpy(config.sta.ssid, connection.ssid, ssid_size);
    memcpy(config.sta.password, connection.password, password_size);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

static bool switch_pending(void);

static bool connect_tv(int fd, const struct addrinfo *address)
{
    if (!relay_link_current(&network_link, session_link) || fcntl(fd, F_SETFL, O_NONBLOCK) != 0)
        return false;
    if (connect(fd, address->ai_addr, address->ai_addrlen) == 0) return true;
    if (errno != EINPROGRESS) return false;
    const int64_t deadline = esp_timer_get_time() + 4000000;
    while (esp_timer_get_time() < deadline && !switch_pending() &&
           relay_link_current(&network_link, session_link)) {
        struct timeval step = {.tv_usec = 100000};
        fd_set writable;
        FD_ZERO(&writable);
        FD_SET(fd, &writable);
        const int ready = select(fd + 1, NULL, &writable, NULL, &step);
        if (!ready) continue;
        int error = 0;
        socklen_t length = sizeof error;
        return ready > 0 && relay_link_current(&network_link, session_link) &&
               getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &length) == 0 && !error;
    }
    return false;
}

static int open_server(void)
{
    const struct addrinfo hints = {.ai_family = AF_INET, .ai_socktype = SOCK_STREAM};
    char port[6];
    snprintf(port, sizeof port, "%u", connection.port);
    struct addrinfo *result;
    if (getaddrinfo(connection.host, port, &hints, &result) != 0) return -1;
    int fd = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (fd >= 0 && !connect_tv(fd, result)) {
        close(fd);
        fd = -1;
    }
    freeaddrinfo(result);
    return fd;
}

bool passport_tv_connection_cancelled(void)
{
    return !relay_link_current(&network_link, session_link) || switch_pending();
}

static void adjust_brightness(void);
static void set_brightness(unsigned level);
static void service_touch(void);
static void draw_controls(void);
static void service_adjustments(void);
static void service_status(void);
static void draw_status_details(ui_surface_t surface);
static bool switch_pending(void)
{
    service_touch();
    service_adjustments();
    service_status();
    unsigned pending = atomic_load(&button_event);
    while (pending != TV_EVENT_NONE) {
        bool interrupts = true;
        if (pending == TV_EVENT_BRIGHTNESS || pending == TV_EVENT_RESUME) {
            interrupts = false;
        } else if (pending == TV_EVENT_CYCLE_PROFILE || pending == TV_EVENT_SELECT_PROFILE) {
            const unsigned requested = pending == TV_EVENT_CYCLE_PROFILE ?
                (quality + 1) % TV_PROFILE_COUNT : selected_quality;
            interrupts = tv_profile_changes(quality, requested, quality_available);
        } else if (pending == TV_EVENT_SELECT_CHANNEL) {
            interrupts = *selected_channel && strcmp(channel, selected_channel);
        } else if (pending == TV_EVENT_NEXT || pending == TV_EVENT_PREVIOUS) {
            char requested[sizeof channel];
            memcpy(requested, channel, sizeof requested);
            interrupts = tv_channel_move(channel_ids, channel_count, requested, sizeof requested,
                                         pending == TV_EVENT_NEXT ? 1 : -1);
        }
        if (interrupts) return true;
        if (atomic_compare_exchange_weak(&button_event, &pending, TV_EVENT_NONE)) {
            if (pending == TV_EVENT_BRIGHTNESS) adjust_brightness();
            pending = atomic_load(&button_event);
        }
    }
    return false;
}

static bool transfer(int fd, uint8_t *data, size_t size, bool write, unsigned timeout_ms)
{
    const int64_t deadline = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    while (size && (write || !switch_pending()) && relay_link_current(&network_link, session_link)) {
        const int64_t remaining = deadline - esp_timer_get_time();
        if (remaining <= 0) break;
        struct timeval step = {.tv_usec = remaining < 100000 ? remaining : 100000};
        fd_set descriptors;
        FD_ZERO(&descriptors);
        FD_SET(fd, &descriptors);
        if (!write && audio_socket >= 0) FD_SET(audio_socket, &descriptors);
        const int max_fd = !write && audio_socket > fd ? audio_socket : fd;
        int ready = select(max_fd + 1, write ? NULL : &descriptors, write ? &descriptors : NULL,
                           NULL, &step);
        if (ready == 0 || (ready < 0 && errno == EINTR)) continue;
        if (ready < 0 || esp_timer_get_time() >= deadline ||
            !relay_link_current(&network_link, session_link)) return false;
        if (!write && audio_socket >= 0 && FD_ISSET(audio_socket, &descriptors))
            service_adjustments();
        if (!FD_ISSET(fd, &descriptors)) continue;
        int n = write ? send(fd, data, size, 0) : recv(fd, data, size, 0);
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) continue;
        if (n <= 0) return false;
        if (!write) received_bytes += (uint32_t)n;
        data += n;
        size -= n;
    }
    return size == 0;
}

static bool send_hello(int fd)
{
    cJSON *json = cJSON_CreateObject();
    if (!json) return false;
    bool ready = cJSON_AddNumberToObject(json, "version", 1)
        && cJSON_AddStringToObject(json, "channel", channel)
        && cJSON_AddStringToObject(json, "quality", tv_profiles[quality].id)
        && (!connection.token[0] || cJSON_AddStringToObject(json, "token", connection.token));
    char *body = ready ? cJSON_PrintUnformatted(json) : NULL;
    cJSON_Delete(json);
    if (!body) return false;
    size_t size = strlen(body);
    uint8_t wire[AV_HEADER_BYTES];
    av_header_t header = {.type = AV_HELLO, .length = size};
    av_header_encode(wire, &header);
    bool ok = size <= AV_CONTROL_MAX && transfer(fd, wire, sizeof wire, true, 4000)
        && transfer(fd, (uint8_t *)body, size, true, 4000);
    cJSON_free(body);
    return ok;
}

static const char *string_at(const cJSON *o, const char *key)
{
    const char *s = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(o, key));
    return s ? s : "";
}

static bool channel_id_valid(const char *id)
{
    size_t len = strlen(id);
    if (len == 0 || len >= AV_CHANNEL_ID_MAX) return false;
    for (size_t i = 0; i < len; ++i) {
        if (id[i] < '!' || id[i] > '~') return false;
    }
    return true;
}

static bool apply_config(const av_header_t *h)
{
    packet[h->length] = 0;
    if (!av_json_depth_safe((char *)packet, h->length, 8)) return false;
    cJSON *config = cJSON_ParseWithLengthOpts((char *)packet, h->length + 1, NULL, true);
    const bool ok = tv_session_config_valid(config, h->session, quality, &quality_available);
    if (ok) {
        if (!quality_available && menu_view == TV_MENU_PROFILES) menu_view = TV_MENU_ADJUSTMENTS;
        const char *chosen = string_at(config, "channel");
        if (channel_id_valid(chosen) && strcmp(channel, chosen)) {
            channel_empty_streak = 0;
            strlcpy(channel, chosen, sizeof channel);
        }
        channel_count = 0;
        const cJSON *entry;
        cJSON_ArrayForEach(entry, cJSON_GetObjectItemCaseSensitive(config, "channel_list")) {
            if (channel_count == AV_CHANNEL_MAX) break;
            const char *id = string_at(entry, "id");
            if (!channel_id_valid(id)) continue;
            if (av_channel_index_of(channel_ids, channel_count, id) != channel_count) continue;
            strlcpy(channel_storage[channel_count], id, sizeof channel_storage[0]);
            channel_ids[channel_count] = channel_storage[channel_count];
            const char *name = string_at(entry, "name");
            ui_text_truncate(channel_names[channel_count], sizeof channel_names[0], name, 190);
            channel_titles[channel_count] = *channel_names[channel_count]
                ? channel_names[channel_count] : channel_storage[channel_count];
            ++channel_count;
        }
        const unsigned current = av_channel_index_of(channel_ids, channel_count, channel);
        if (channel_count) {
            menu_page = tv_menu_restore_page(menu_page, channel_count, current, menu_page_visited);
            if (menu_view == TV_MENU_CHANNELS) menu_page_visited = true;
        }
        if (channel_id_valid(chosen)) {
            tv_channel_notice_confirm(&channel_notice, chosen,
                                      current < channel_count ? channel_titles[current] : chosen);
        }
        draw_controls();
        ESP_LOGI(TAG, "Channel %s / %s (%u channels)", channel, tv_profiles[quality].id, channel_count);
    }
    cJSON_Delete(config);
    return ok;
}

static esp_err_t panel_draw(int x, int y, int w, int h, const void *pixels)
{
    if (panel_lock) xSemaphoreTake(panel_lock, portMAX_DELAY);
    const esp_err_t result = esp_lcd_panel_draw_bitmap(panel, x, y, x + w, y + h, pixels);
    if (panel_lock) xSemaphoreGive(panel_lock);
    return result;
}

static bool show_pixels(const uint16_t *pixels)
{
    if (panel_lock) xSemaphoreTake(panel_lock, portMAX_DELAY);
    const bool controls = atomic_load(&controls_visible);
    const int top = controls ? TV_STATUS_Y + TV_STATUS_ROWS +
        (atomic_load(&volume_notice_visible) ? TV_VOLUME_NOTICE_ROWS : 0) : 0;
    const int bottom = controls ? TV_MENU_BOTTOM_Y : BSP_LCD_V_RES;
    tv_fill_video_rows(lcd_pixels, pixels, scanline, top, bottom);
    const esp_err_t result = esp_lcd_panel_draw_bitmap(panel, 0, top, BSP_LCD_H_RES, bottom, lcd_pixels);
    if (panel_lock) xSemaphoreGive(panel_lock);
    return result == ESP_OK;
}

static bool draw_control_band(ui_surface_t surface)
{
    /* Upstream text painters emit big-endian RGB565. Indicator uses native
       little-endian uint16_t, the same order as the video palette output. */
    const size_t bytes = (size_t)surface.width * surface.rows * 2;
    for (size_t i = 0; i < bytes; i += 2) {
        const uint8_t hi = surface.pixels[i];
        surface.pixels[i] = surface.pixels[i + 1];
        surface.pixels[i + 1] = hi;
    }
    if (panel_draw(0, surface.origin_y, surface.width, surface.rows, surface.pixels) == ESP_OK) return true;
    ESP_LOGW(TAG, "Could not draw channel controls");
    return false;
}

static void service_volume_notice(int64_t now)
{
    const int level = tv_volume_notice_level(menu_view == TV_MENU_ADJUSTMENTS, now,
                                             volume_notice_until_us, sound.level);
    if (level == volume_notice_painted) return;
    atomic_store(&volume_notice_visible, level >= 0);
    if (level < 0) {
        volume_notice_painted = level;
        return;
    }
    const ui_surface_t surface = {.pixels = control_pixels, .width = TV_MENU_WIDTH,
                                  .origin_y = TV_VOLUME_NOTICE_Y, .rows = TV_VOLUME_NOTICE_ROWS};
    tv_volume_notice_draw(surface, level);
    if (draw_control_band(surface)) volume_notice_painted = level;
}

static tv_stream_totals_t stream_totals(void)
{
    return (tv_stream_totals_t){.frames = atomic_load_explicit(&displayed_frames, memory_order_relaxed),
                                .bytes = received_bytes};
}

static void draw_status(void)
{
    if (menu_view == TV_MENU_HIDDEN) return;
    const ui_surface_t surface = {.pixels = control_pixels, .width = TV_MENU_WIDTH,
                                  .origin_y = TV_STATUS_Y, .rows = TV_STATUS_ROWS};
    tv_status_draw(surface, stream_status.reading);
    draw_control_band(surface);
}

static void service_status(void)
{
    if (!tv_status_sample(&stream_status, esp_timer_get_time(), stream_totals(), stream_active)) return;
    wifi_ap_record_t access_point;
    stream_status.reading.wifi_rssi =
        (xEventGroupGetBits(network) & BIT0) && esp_wifi_sta_get_ap_info(&access_point) == ESP_OK
        ? access_point.rssi : 0;
    stream_status.reading.wifi_bars = tv_wifi_bars(stream_status.reading.wifi_rssi);
    draw_status();
    if (menu_view == TV_MENU_STATUS) {
        const ui_surface_t surface = {.pixels = control_pixels, .width = TV_MENU_WIDTH,
                                      .origin_y = TV_MENU_BOTTOM_Y, .rows = TV_MENU_BOTTOM_ROWS};
        draw_status_details(surface);
        draw_control_band(surface);
    }
}

static void draw_status_details(ui_surface_t surface)
{
    const bool online = network && (xEventGroupGetBits(network) & BIT0);
    const bool current_link = online && relay_link_current(&network_link, session_link);
    const bool recovering = tv_view_recovering(atomic_load(&display_view));
    const bool playing = current_link && stream_active;
    const tv_link_status_t link = tv_status_link(online, playing, recovering, stream_status.reading.fps_tenths);
    const tv_audio_status_t audio = tv_status_audio(audio_peer[0], playing ? clock_session : 0,
                                                    esp_timer_get_time(), speaker_synced_us);
    char address[16] = "--";
    esp_netif_ip_info_t ip;
    if (online && esp_netif_get_ip_info(wifi_station, &ip) == ESP_OK && ip.ip.addr)
        snprintf(address, sizeof address, IPSTR, IP2STR(&ip.ip));
    tv_menu_status(surface, online ? stream_status.reading.wifi_rssi : 0, address, link, audio);
}

static void draw_controls(void)
{
    atomic_store(&channel_notice_visible, menu_view == TV_MENU_ADJUSTMENTS);
    atomic_store(&controls_visible, menu_view != TV_MENU_HIDDEN);
    if (!control_pixels || menu_view == TV_MENU_HIDDEN) return;
    const unsigned current = av_channel_index_of(channel_ids, channel_count, channel);
    const char *title = current < channel_count ? channel_titles[current] : *channel ? channel : "等待频道";
    ui_surface_t surface = {.pixels = control_pixels, .width = TV_MENU_WIDTH, .rows = TV_MENU_TOP_ROWS};
    tv_menu_top(surface, title, quality_available ? tv_profiles[quality].target_fps : 0, menu_view);
    draw_control_band(surface);
    draw_status();
    surface.origin_y = TV_MENU_BOTTOM_Y;
    surface.rows = TV_MENU_BOTTOM_ROWS;
    if (menu_view == TV_MENU_CHANNELS) {
        tv_menu_bottom(surface, true, menu_page, channel_count, current, channel_titles);
    } else if (menu_view == TV_MENU_PROFILES) {
        tv_menu_profiles(surface, quality);
    } else if (menu_view == TV_MENU_PAUSED) {
        tv_menu_paused(surface);
    } else if (menu_view == TV_MENU_STATUS) {
        draw_status_details(surface);
    } else {
        tv_menu_adjustments(surface, sound.level, brightness, audio_peer[0] != '\0');
    }
    draw_control_band(surface);
}

static void service_touch(void)
{
    const int64_t now = esp_timer_get_time();
    if (tv_menu_expired(menu_view, menu_deadline_us, now)) {
        menu_view = TV_MENU_HIDDEN;
        menu_closed_us = now;
        menu_deadline_us = 0;
        draw_controls();
    }
    tv_touch_event_t event;
    if (!touch_events || xQueueReceive(touch_events, &event, 0) != pdTRUE ||
        event.started_us < menu_closed_us) return;
    unsigned selected = 0;
    const tv_menu_action_t action = tv_menu_touch(menu_view, menu_page, channel_count, event, &selected);
    switch (action) {
    case TV_MENU_SHOW:
        menu_view = TV_MENU_ADJUSTMENTS;
        break;
    case TV_MENU_HIDE:
        menu_view = TV_MENU_HIDDEN;
        menu_closed_us = now;
        break;
    case TV_MENU_PAUSE:
        atomic_store(&button_event, TV_EVENT_PAUSE);
        return;
    case TV_MENU_RESUME:
        atomic_store(&button_event, TV_EVENT_RESUME);
        return;
    case TV_MENU_SETUP:
        atomic_store(&button_event, TV_EVENT_SETUP);
        return;
    case TV_MENU_TOGGLE_STATUS:
        menu_view = menu_view == TV_MENU_STATUS ? TV_MENU_ADJUSTMENTS : TV_MENU_STATUS;
        break;
    case TV_MENU_TOGGLE:
        menu_view = menu_view == TV_MENU_CHANNELS ? TV_MENU_ADJUSTMENTS : TV_MENU_CHANNELS;
        if (menu_view == TV_MENU_CHANNELS && channel_count) {
            const unsigned current = av_channel_index_of(channel_ids, channel_count, channel);
            menu_page = tv_menu_restore_page(menu_page, channel_count, current, menu_page_visited);
            menu_page_visited = true;
        }
        break;
    case TV_MENU_QUALITY:
        if (!quality_available) return;
        menu_view = menu_view == TV_MENU_PROFILES ? TV_MENU_ADJUSTMENTS : TV_MENU_PROFILES;
        break;
    case TV_MENU_PROFILE:
        menu_view = TV_MENU_ADJUSTMENTS;
        if (quality != selected) {
            selected_quality = selected;
            atomic_store(&button_event, TV_EVENT_SELECT_PROFILE);
        }
        break;
    case TV_MENU_VOL_DOWN:
    case TV_MENU_VOL_UP: {
        if (!audio_peer[0]) return;
        const int64_t now = esp_timer_get_time();
        volume_notice_until_us = now + TV_VOLUME_NOTICE_US;
        const unsigned level = tv_menu_level(sound.level, 0, action == TV_MENU_VOL_UP);
        if (level == sound.level) return;
        sound.level = level;
        if (++sound.revision == 0) ++sound.revision;
        next_volume_send = 0;
        save_controls_due = now + 700000;
        break;
    }
    case TV_MENU_DIM:
    case TV_MENU_BRIGHTEN: {
        const unsigned level = tv_menu_level(brightness, 10, action == TV_MENU_BRIGHTEN);
        if (level == brightness) return;
        set_brightness(level);
        break;
    }
    case TV_MENU_PREVIOUS: --menu_page; break;
    case TV_MENU_NEXT: ++menu_page; break;
    case TV_MENU_CHANNEL:
        menu_view = TV_MENU_ADJUSTMENTS;
        if (strcmp(channel_ids[selected], channel)) {
            strlcpy(selected_channel, channel_ids[selected], sizeof selected_channel);
            atomic_store(&button_event, TV_EVENT_SELECT_CHANNEL);
        }
        break;
    case TV_MENU_CHANNEL_FORWARD:
    case TV_MENU_CHANNEL_BACK:
        atomic_store(&button_event, action == TV_MENU_CHANNEL_FORWARD ? TV_EVENT_NEXT : TV_EVENT_PREVIOUS);
        return;
    case TV_MENU_NONE: return;
    }
    menu_deadline_us = tv_menu_deadline(menu_view, now);
    draw_controls();
}

static void poll_touch(void *arg)
{
    (void)arg;
    esp_lcd_touch_handle_t touch;
    if (bsp_touch_init(&touch) != ESP_OK) {
        ESP_LOGW(TAG, "Touch unavailable, use the case button");
        vTaskDelete(NULL);
        return;
    }
    tv_touch_tracker_t tracker = {0};
    int64_t started_us = 0;
    for (;;) {
        esp_lcd_touch_point_data_t points[2] = {0};
        uint8_t count = 0;
        esp_err_t err = esp_lcd_touch_read_data(touch);
        if (err == ESP_OK) err = esp_lcd_touch_get_data(touch, points, &count, 2);
        const bool valid = err == ESP_OK && count <= 1 &&
            (!count || (points[0].x < BSP_LCD_H_RES && points[0].y < BSP_LCD_V_RES));
        if (valid && count && !tracker.pressed) started_us = esp_timer_get_time();
        tv_touch_event_t event;
        if (tv_touch_sample(&tracker, valid, count != 0, points[0].x, points[0].y, &event)) {
            event.started_us = started_us;
            xQueueOverwrite(touch_events, &event);
        }
        vTaskDelay(pdMS_TO_TICKS(25));
    }
}

static void present_notice(int64_t now, int64_t *next_notice, uint32_t *seed, tv_view_mode_t *painted)
{
    const unsigned state = atomic_load(&display_view);
    const tv_view_mode_t mode = tv_view_mode(state);
    if (mode == TV_VIEW_PICTURE) {
        *painted = mode;
        *next_notice = 0;
        return;
    }
    if (mode == TV_VIEW_RECONNECTING) {
        if (!tv_reconnect_due(state, now, next_notice)) return;
        tv_reconnect_draw(rgb, seed);
    } else {
        *next_notice = 0;
        if (*painted == mode) return;
        tv_tuning_draw(rgb, mode);
    }
    if (state != atomic_load(&display_view)) return;
    if (show_pixels(rgb)) *painted = mode;
    else ESP_LOGW(TAG, "Waiting display failed");
}

static void present_video(void *arg)
{
    (void)arg;
    display_frame_t item;
    uint32_t seed = UINT32_C(0x49ae37cb);
    int64_t next_notice = 0;
    tv_view_mode_t painted = TV_VIEW_PICTURE;
    for (;;) {
        if (xQueueReceive(video_ready, &item, pdMS_TO_TICKS(50)) != pdTRUE) {
            present_notice(esp_timer_get_time(), &next_notice, &seed, &painted);
            continue;
        }
        while (item.generation == tv_view_generation(atomic_load(&display_view))) {
            const int64_t now = esp_timer_get_time();
            const int64_t remaining = item.due_us + atomic_load(&clock_correction_us) - now;
            if (remaining <= 0) {
                if (remaining >= -VIDEO_LATE_US) {
                    if (item.notice_until_us > now && atomic_load(&channel_notice_visible))
                        tv_channel_notice_draw(item.pixels, &item.notice_title);
                    if (show_pixels(item.pixels)) {
                        painted = TV_VIEW_PICTURE;
                        tv_view_presented(&display_view, item.generation);
                        if (item.generation == tv_view_generation(atomic_load(&display_view)))
                            atomic_fetch_add_explicit(&displayed_frames, 1, memory_order_relaxed);
                    } else ESP_LOGW(TAG, "Display frame failed");
                }
                break;
            }
            present_notice(now, &next_notice, &seed, &painted);
            vTaskDelay(1);
        }
        ESP_ERROR_CHECK(xQueueSend(video_free, &item.pixels, 0) == pdTRUE ? ESP_OK : ESP_FAIL);
    }
}

static void start_video_queue(void)
{
    video_ready = xQueueCreate(VIDEO_SLOTS, sizeof(display_frame_t));
    video_free = xQueueCreate(VIDEO_SLOTS, sizeof(uint16_t *));
    ESP_ERROR_CHECK(video_ready && video_free ? ESP_OK : ESP_ERR_NO_MEM);
    for (unsigned i = 0; i < VIDEO_SLOTS; ++i) {
        uint16_t *pixels = heap_caps_malloc(FRAME_PIXELS * sizeof *pixels, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        ESP_ERROR_CHECK(pixels && xQueueSend(video_free, &pixels, 0) == pdTRUE ? ESP_OK : ESP_ERR_NO_MEM);
    }
    ESP_ERROR_CHECK(xTaskCreate(present_video, "tv_display", 4096, NULL, 4, NULL) == pdPASS
                    ? ESP_OK : ESP_ERR_NO_MEM);
}

static void queue_frame(const uint16_t *palette, const video_clock_t *clock, uint32_t pts)
{
    display_frame_t item = {.generation = tv_view_generation(atomic_load(&display_view))};
    if (!video_clock_due(clock, pts, esp_timer_get_time(), &item.due_us) ||
        xQueueReceive(video_free, &item.pixels, 0) != pdTRUE) return;
    if (!strcmp(channel_notice.channel, channel) && tv_channel_notice_frame(&channel_notice, item.due_us)) {
        item.notice_until_us = channel_notice.until_us;
        item.notice_title = channel_notice.title;
    }
    for (size_t i = 0; i < FRAME_PIXELS; ++i) item.pixels[i] = palette[frame[i]];
    ESP_ERROR_CHECK(xQueueSend(video_ready, &item, 0) == pdTRUE ? ESP_OK : ESP_FAIL);
}

static bool inflate_stripe(void *context, const uint8_t *data, size_t length, uint8_t *pixels)
{
    tinfl_decompressor *decoder = context;
    size_t consumed = length, produced = AV_STRIPE_PIXELS;
    tinfl_init(decoder);
    tinfl_status status = tinfl_decompress(decoder, data, &consumed, pixels, pixels,
        &produced, TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
    return status == TINFL_STATUS_DONE && consumed == length && produced == AV_STRIPE_PIXELS;
}

static bool play(int fd, uint32_t *relay_session, uint32_t *relay_end_pts, bool *media_seen)
{
    av_stream_t stream = {0};
    video_clock = (video_clock_t){0};
    atomic_store(&clock_correction_us, 0);
    clock_session = 0;
    speaker_synced_us = 0;
    clock_probe_us = 0;
    next_clock_probe = 0;
    uint16_t palette[AV_PALETTE_ENTRIES];
    bool palette_ready = false;
    av_picture_t picture = {0};
    memset(frame, 0, FRAME_PIXELS);
    if (!send_hello(fd)) return false;
    for (;;) {
        const unsigned timeout_ms = tv_header_timeout_ms(stream.configured, *media_seen);
        av_header_t h;
        uint8_t wire[AV_HEADER_BYTES];
        if (!transfer(fd, wire, sizeof wire, false, timeout_ms) || !av_header_decode(wire, &h)
            || !av_stream_accept(&stream, &h) || !transfer(fd, packet, h.length, false, AV_READ_DEADLINE_MS)) {
            if (stream.configured && switch_pending()) {
                tv_view_advance(&display_view, TV_VIEW_KEEP);
                const av_header_t end = {.type = AV_END, .session = stream.session};
                av_header_encode(wire, &end);
                return transfer(fd, wire, sizeof wire, true, 1000);
            }
            return false;
        }
        if (h.type == AV_CONFIG) {
            if (!apply_config(&h)) return false;
            tv_view_connected(&display_view);
            *relay_session = h.session;
            clock_session = h.session;
            tv_status_reset(&stream_status, esp_timer_get_time(), stream_totals());
            stream_active = true;
        } else if (h.type == AV_PALETTE) {
            av_palette_decode(packet, palette);
            palette_ready = true;
        } else if (h.type == AV_AUDIO) {
            if (!av_adpcm_header_valid(packet)) return false;
            *media_seen = true;
            video_clock_audio(&video_clock, h.pts_ms, esp_timer_get_time());
            atomic_store(&clock_correction_us, video_clock.correction_us);
            if (audio_socket >= 0 && relay_link_current(&network_link, session_link)) {
                uint8_t audio[AV_HEADER_BYTES + AV_AUDIO_BYTES];
                memcpy(audio, wire, sizeof wire);
                memcpy(audio + sizeof wire, packet, AV_AUDIO_BYTES);
                send(audio_socket, audio, sizeof audio, MSG_DONTWAIT);
            }
        } else if (h.type == AV_VIDEO) {
            if (!palette_ready) return false;
            const av_picture_result_t result = av_picture_packet(&picture, frame, &h, packet, inflate_stripe, inflater);
            if (result == AV_PICTURE_INVALID) return false;
            if (result == AV_PICTURE_COMPLETE) {
                *media_seen = true;
                video_clock_start(&video_clock, picture.pts_ms, esp_timer_get_time());
                queue_frame(palette, &video_clock, picture.pts_ms);
            }
        } else {
            if (h.type == AV_END && stream.audio_seen) *relay_end_pts = stream.audio_next_pts;
            return h.type == AV_END;
        }
    }
}

static void end_audio(uint32_t session, uint32_t next_pts)
{
    if (audio_socket < 0 || !session || !relay_link_current(&network_link, session_link)) return;
    /* A nonzero endpoint drains the final buffered blocks; zero cancels. */
    const av_header_t end = {.type = AV_END, .session = session, .pts_ms = next_pts};
    uint8_t wire[AV_HEADER_BYTES];
    av_header_encode(wire, &end);
    send(audio_socket, wire, sizeof wire, MSG_DONTWAIT);
}

static void configure_backlight(void)
{
    /* Sourced from CodeCraft's Indicator BSP pin contract. The app owns PWM
       after panel initialization and does not use the BSP GPIO setter then. */
    const ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE, .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = LEDC_TIMER_0, .freq_hz = 5000, .clk_cfg = LEDC_AUTO_CLK,
    };
    const ledc_channel_config_t output = {
        .gpio_num = BSP_PIN_LCD_BACKLIGHT, .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0, .timer_sel = LEDC_TIMER_0, .duty = brightness * 255 / 100,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));
    ESP_ERROR_CHECK(ledc_channel_config(&output));
}

static void set_brightness(unsigned level)
{
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, level * 255 / 100));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0));
    brightness = level;
    save_controls_due = esp_timer_get_time() + 700000;
    ESP_LOGI(TAG, "Backlight brightness %u%%", brightness);
}

static void adjust_brightness(void)
{
    set_brightness(brightness == 10 ? 100 : brightness - 10);
    draw_controls();
}

static void load_adjustments(void)
{
    sound.generation = esp_random();
    if (!sound.generation) sound.generation = 1;
    nvs_handle_t store;
    if (nvs_open("tv_controls", NVS_READONLY, &store) != ESP_OK) return;
    uint8_t value;
    if (nvs_get_u8(store, "brightness", &value) == ESP_OK &&
        value >= 10 && value <= 100 && value % 10 == 0) brightness = value;
    if (nvs_get_u8(store, "volume", &value) == ESP_OK &&
        value <= 100 && value % 10 == 0) sound.level = value;
    nvs_close(store);
}

static unsigned load_profile(void)
{
    nvs_handle_t store;
    uint8_t saved = TV_ULTRA;
    if (nvs_open("ft_tv", NVS_READONLY, &store) == ESP_OK) {
        (void)nvs_get_u8(store, "quality", &saved);
        nvs_close(store);
    }
    return tv_profile_restore(saved);
}

static void set_profile(unsigned next)
{
    if (!tv_profile_changes(quality, next, quality_available)) return;
    quality = next;
    nvs_handle_t store;
    esp_err_t status = nvs_open("ft_tv", NVS_READWRITE, &store);
    if (status == ESP_OK) {
        status = nvs_set_u8(store, "quality", (uint8_t)quality);
        if (status == ESP_OK) status = nvs_commit(store);
        nvs_close(store);
    }
    if (status != ESP_OK) ESP_LOGW(TAG, "TV quality could not be saved: %s", esp_err_to_name(status));
}

static void service_adjustments(void)
{
    const int64_t now = esp_timer_get_time();
    service_volume_notice(now);
    if (save_controls_due && now >= save_controls_due) {
        nvs_handle_t store;
        esp_err_t status = nvs_open("tv_controls", NVS_READWRITE, &store);
        if (status == ESP_OK) {
            status = nvs_set_u8(store, "brightness", brightness);
            if (status == ESP_OK) status = nvs_set_u8(store, "volume", sound.level);
            if (status == ESP_OK) status = nvs_commit(store);
            nvs_close(store);
        }
        save_controls_due = status == ESP_OK ? 0 : now + 5000000;
        if (status != ESP_OK) ESP_LOGW(TAG, "TV controls could not be saved: %s", esp_err_to_name(status));
    }
    if (audio_socket < 0 || !relay_link_current(&network_link, session_link)) return;
    uint8_t wire[RELAY_TIMING_BYTES + 1];
    for (unsigned i = 0; i < 4; ++i) {
        const int n = recv(audio_socket, wire, sizeof wire, MSG_DONTWAIT);
        if (n <= 0) break;
        relay_volume_t ack;
        relay_timing_t timing;
        if (relay_volume_decode(wire, n, RELAY_VOLUME_ACK, &ack) && relay_volume_matches(ack, sound)) {
            next_volume_send = now + 2000000;
        } else if (clock_probe_us && relay_timing_decode(wire, n, &timing) &&
                   timing.pts_ms % AV_AUDIO_MS == 0 &&
                   video_clock_sync(&video_clock, timing, clock_session, clock_token,
                                    clock_probe_us, esp_timer_get_time())) {
            atomic_store(&clock_correction_us, video_clock.correction_us);
            speaker_synced_us = now;
            clock_probe_us = 0;
            next_clock_probe = now + 1000000;
        }
    }
    if (now >= next_volume_send) {
        relay_volume_encode(wire, RELAY_VOLUME_SET, sound);
        send(audio_socket, wire, RELAY_VOLUME_BYTES, MSG_DONTWAIT);
        next_volume_send = now + 200000;
    }
    if (clock_session && video_clock.audio_anchored && now >= next_clock_probe) {
        if (++clock_token == 0) ++clock_token;
        const relay_timing_t probe = {.token = clock_token, .session = clock_session,
                                      .delay_us = RELAY_TIMING_PROBE};
        relay_timing_encode(wire, probe);
        clock_probe_us = esp_timer_get_time();
        send(audio_socket, wire, RELAY_TIMING_BYTES, MSG_DONTWAIT);
        next_clock_probe = now + (video_clock.remote ? 1000000 : 120000);
    }
}

static void open_audio_peer(void)
{
    struct sockaddr_in peer = {.sin_family = AF_INET, .sin_port = htons(audio_port)};
    if (!relay_link_current(&network_link, session_link) ||
        inet_pton(AF_INET, audio_peer, &peer.sin_addr) != 1) return;
    int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd < 0) return;
    if (connect(fd, (const struct sockaddr *)&peer, sizeof peer) != 0) {
        close(fd);
        return;
    }
    audio_socket = fd;
    next_volume_send = 0;
}

static void close_audio_peer(void)
{
    if (audio_socket >= 0) close(audio_socket);
    audio_socket = -1;
}

static void setup_text(int y, const char *text, uint16_t foreground)
{
    enum { SCALE = 3, HEIGHT = 28, MARGIN = 48, ADVANCE = 6 * SCALE };
    const int width = BSP_LCD_H_RES;
    if (y < 0 || y + HEIGHT > BSP_LCD_V_RES) return;
    // CodeCraft's RGB BSP copies the buffer before returning from a draw.
    for (int i = 0; i < width * HEIGHT; ++i) rgb[i] = 0x0842;
    for (int col = 0; text[col] && MARGIN + (col + 1) * ADVANCE <= width; ++col) {
        const uint8_t *bits = glyph(text[col]);
        for (int x = 0; x < 5; ++x) {
            for (int row = 0; row < 7; ++row) {
                if (!(bits[x] & (1u << row))) continue;
                for (int dy = 0; dy < SCALE; ++dy) {
                    for (int dx = 0; dx < SCALE; ++dx) {
                        const int py = 3 + row * SCALE + dy;
                        const int px = MARGIN + col * ADVANCE + x * SCALE + dx;
                        rgb[py * width + px] = foreground;
                    }
                }
            }
        }
    }
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel, 0, y, width, y + HEIGHT, rgb));
}

static void draw_setup(const char *ssid)
{
    setup_text(76, "TV RECEIVER SETUP", 0x07ff);
    setup_text(132, "JOIN WI-FI", 0x9cf3);
    setup_text(160, ssid, 0xffff);
    setup_text(216, "NO PASSWORD", 0x9cf3);
    setup_text(300, "OPEN IN BROWSER", 0x9cf3);
    setup_text(328, "192.168.4.1", 0xffff);
}

static bool configure_inputs(void)
{
    portal_config_t saved = {0};
    if (portal_config_read(&saved)) {
        connection = saved.tv;
        memcpy(origin, saved.origin, sizeof origin);
        memcpy(pairing_code, saved.code, sizeof pairing_code);
        memcpy(audio_peer, saved.audio_peer, sizeof audio_peer);
        audio_port = saved.audio_port;
        return true;
    }
    audio_port = CONFIG_PASSPORT_TV_AUDIO_PORT;
    if (strlcpy(audio_peer, CONFIG_PASSPORT_TV_AUDIO_PEER, sizeof audio_peer) >= sizeof audio_peer ||
        strlcpy(pairing_code, CONFIG_PASSPORT_TV_PAIRING_CODE, sizeof pairing_code) >= sizeof pairing_code)
        return false;
    const char *ssid = CONFIG_PASSPORT_TV_SSID;
    const char *password = CONFIG_PASSPORT_TV_PASSWORD;
    const char *host = CONFIG_PASSPORT_TV_HOST;
    const char *token = CONFIG_PASSPORT_TV_TOKEN;
    if (!*ssid || strlcpy(connection.ssid, ssid, sizeof connection.ssid) >= sizeof connection.ssid ||
        strlcpy(connection.password, password, sizeof connection.password) >= sizeof connection.password)
        return false;
    if (CONFIG_PASSPORT_TV_PAIRING_ORIGIN[0]) {
        return tv_origin_from_input(CONFIG_PASSPORT_TV_PAIRING_ORIGIN, CONFIG_PASSPORT_TV_PORT,
                                    NULL, origin);
    }
    connection.port = CONFIG_PASSPORT_TV_PORT;
    return strlcpy(connection.host, host, sizeof connection.host) < sizeof connection.host &&
        strlcpy(connection.token, token, sizeof connection.token) < sizeof connection.token &&
        tv_host_valid(connection.host, sizeof connection.host);
}

static bool setup_requested(void)
{
    nvs_handle_t store;
    uint8_t requested = 0;
    if (nvs_open("ptv_setup", NVS_READWRITE, &store) != ESP_OK) return false;
    if (nvs_get_u8(store, "open_next", &requested) == ESP_OK) {
        esp_err_t status = nvs_erase_key(store, "open_next");
        if (status == ESP_OK) status = nvs_commit(store);
        if (status != ESP_OK) {
            requested = 0;
            ESP_LOGW(TAG, "Could not consume setup request: %s", esp_err_to_name(status));
        }
    }
    nvs_close(store);
    return requested == 1;
}

static void restart_into_setup(void)
{
    nvs_handle_t store;
    esp_err_t status = nvs_open("ptv_setup", NVS_READWRITE, &store);
    if (status == ESP_OK) {
        status = nvs_set_u8(store, "open_next", 1);
        if (status == ESP_OK) status = nvs_commit(store);
        nvs_close(store);
    }
    if (status == ESP_OK) esp_restart();
    ESP_LOGW(TAG, "Could not request setup: %s", esp_err_to_name(status));
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    const bool open_setup = setup_requested();
    load_adjustments();
    quality = load_profile();
    ESP_ERROR_CHECK(bsp_display_init(&panel));
    void *panel_pixels = NULL;
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_get_frame_buffer(panel, 1, &panel_pixels));
    lcd_pixels = panel_pixels;
    configure_backlight();
    ESP_ERROR_CHECK(bsp_button_init(button));
    packet = heap_caps_malloc(PACKET_CAP, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    frame = heap_caps_malloc(FRAME_PIXELS, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    rgb = heap_caps_calloc(FRAME_PIXELS, sizeof *rgb, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    inflater = heap_caps_malloc(sizeof *inflater, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    configASSERT(packet && frame && rgb && inflater);
    configASSERT(show_pixels(rgb));
    vTaskDelay(pdMS_TO_TICKS(300));
    if (open_setup || bsp_button_pressed() || !configure_inputs()) {
        portal_serve(draw_setup);
        return;
    }
    panel_lock = xSemaphoreCreateMutex();
    control_pixels = heap_caps_malloc(TV_MENU_WIDTH * TV_MENU_BOTTOM_ROWS * 2,
                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    touch_events = xQueueCreate(1, sizeof(tv_touch_event_t));
    ESP_ERROR_CHECK(panel_lock && control_pixels && touch_events ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(xTaskCreate(poll_touch, "tv_touch", 4096, NULL, 2, NULL) == pdPASS
                    ? ESP_OK : ESP_ERR_NO_MEM);
    start_video_queue();
    tv_view_advance(&display_view, TV_VIEW_TUNING);
    start_wifi();
    const int64_t first_connect = esp_timer_get_time();
    tv_status_reset(&stream_status, first_connect, stream_totals());
    for (;;) {
        service_touch();
        service_adjustments();
        service_status();
        const tv_button_event_t event = atomic_exchange(&button_event, TV_EVENT_NONE);
        if (event == TV_EVENT_SETUP) {
            restart_into_setup();
            continue;
        }
        if (menu_view == TV_MENU_PAUSED) {
            if (event == TV_EVENT_CYCLE_PROFILE ||
                event == TV_EVENT_BRIGHTNESS || event == TV_EVENT_PAUSE) {
                restart_into_setup();
            } else if (event == TV_EVENT_RESUME || event == TV_EVENT_NEXT) {
                menu_view = TV_MENU_HIDDEN;
                menu_deadline_us = 0;
                draw_controls();
                continue;
            }
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        if (event == TV_EVENT_PAUSE) {
            tv_view_advance(&display_view, TV_VIEW_PICTURE);
            menu_view = TV_MENU_PAUSED;
            draw_controls();
            continue;
        }
        if (event == TV_EVENT_CYCLE_PROFILE) {
            set_profile((quality + 1) % TV_PROFILE_COUNT);
        } else if (event == TV_EVENT_BRIGHTNESS) {
            adjust_brightness();
        } else if (event == TV_EVENT_NEXT || event == TV_EVENT_PREVIOUS) {
            if (tv_channel_move(channel_ids, channel_count, channel, sizeof channel,
                                event == TV_EVENT_NEXT ? 1 : -1)) {
                channel_empty_streak = 0;
                channel_chosen_by_user = true;
                tv_view_advance(&display_view, TV_VIEW_TUNING);
            }
        } else if (event == TV_EVENT_SELECT_CHANNEL && *selected_channel && strcmp(channel, selected_channel)) {
            strlcpy(channel, selected_channel, sizeof channel);
            channel_empty_streak = 0;
            channel_chosen_by_user = true;
            tv_view_advance(&display_view, TV_VIEW_TUNING);
        } else if (event == TV_EVENT_SELECT_PROFILE) {
            set_profile(selected_quality);
        }
        if (event != TV_EVENT_NONE && event != TV_EVENT_BRIGHTNESS) draw_controls();
        if (!(xEventGroupWaitBits(network, BIT0, pdFALSE, pdTRUE, pdMS_TO_TICKS(100)) & BIT0)) {
            channel_empty_streak = 0;
            if (esp_timer_get_time() - first_connect >= 4000000 &&
                !tv_view_recovering(atomic_load(&display_view)))
                tv_view_advance(&display_view, TV_VIEW_LINK_LOST);
            continue;
        }
        session_link = atomic_load(&network_link);
        if (!relay_link_current(&network_link, session_link)) continue;
        const esp_err_t authorization = tv_prepare_session(origin, pairing_code, &connection);
        if (authorization == ESP_OK && origin[0] && pairing_code[0]) {
            if (!portal_consume_pairing_code(origin, pairing_code))
                ESP_LOGW(TAG, "Could not clear the used pairing code from settings");
            memset(pairing_code, 0, sizeof pairing_code);
        }
        if (!relay_link_current(&network_link, session_link) || switch_pending()) {
            channel_empty_streak = 0;
            continue;
        }
        if (authorization != ESP_OK)
            ESP_LOGW(TAG, "Service pairing unavailable: %s", esp_err_to_name(authorization));
        const int fd = authorization == ESP_OK ? open_server() : -1;
        bool failed = true;
        bool media_seen = false;
        if (fd >= 0) {
            open_audio_peer();
            uint32_t relay_session = 0, relay_end_pts = 0;
            tv_view_advance(&display_view, TV_VIEW_KEEP);
            failed = !play(fd, &relay_session, &relay_end_pts, &media_seen);
            const bool switching = switch_pending();
            if (failed || switching)
                tv_view_advance(&display_view, switching ? TV_VIEW_KEEP : TV_VIEW_LINK_LOST);
            end_audio(relay_session, switching || failed ? 0 : relay_end_pts);
            clock_session = 0;
            close_audio_peer();
            close(fd);
            stream_active = false;
            tv_status_reset(&stream_status, esp_timer_get_time(), stream_totals());
            if (!(xEventGroupGetBits(network) & BIT0)) {
                stream_status.reading.wifi_bars = 0;
                stream_status.reading.wifi_rssi = 0;
            }
            draw_status();
        } else if (!switch_pending()) {
            tv_view_advance(&display_view, TV_VIEW_LINK_LOST);
        }
        const bool reached_server = fd >= 0 && relay_link_current(&network_link, session_link) && !switch_pending();
        const unsigned retry_limit = channel_chosen_by_user ? 5 : 3;
        if (av_channel_policy_should_skip(&channel_history, channel, reached_server, media_seen,
                                          &channel_empty_streak, retry_limit, channel_count)) {
            ESP_LOGW(TAG, "Channel %s has not produced media; trying the next channel", channel);
            if (tv_channel_move(channel_ids, channel_count, channel, sizeof channel, 1)) {
                channel_chosen_by_user = false;
                tv_view_advance(&display_view, TV_VIEW_TUNING);
                draw_controls();
            }
        } else if (media_seen) {
            channel_chosen_by_user = false;
        }
        if (failed && !atomic_load(&button_event)) ESP_LOGW(TAG, "Stream interrupted");
        const int64_t retry_at = esp_timer_get_time() + 1000000;
        while (esp_timer_get_time() < retry_at && !switch_pending()) vTaskDelay(pdMS_TO_TICKS(50));
    }
}
