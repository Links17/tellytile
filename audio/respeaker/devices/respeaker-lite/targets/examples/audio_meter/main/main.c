#include "audio_playout.h"
#include "relay_timing.h"
#include "relay_link.h"
#include "relay_volume.h"
#include "bsp_audio.h"
#include "bsp_button.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "speaker_setup.h"

#include <stdatomic.h>
#include <string.h>
#include <unistd.h>

static const char *TAG = "live_tv_audio";
static EventGroupHandle_t network;
static atomic_uint network_link;
static atomic_bool volume_pending;
static unsigned volume_percent = CONFIG_PASSPORT_TV_VOLUME;
static relay_volume_t last_volume;
static uint32_t previous_generation;

static void on_volume_button(bool pressed, void *arg)
{
    (void)arg;
    if (pressed) atomic_store(&volume_pending, true);
}

static void update_volume(void)
{
    if (!atomic_exchange(&volume_pending, false)) return;
    unsigned next = volume_percent >= 75 ? 0 : volume_percent + 15;
    if (bsp_audio_set_volume(next) == ESP_OK) volume_percent = next;
}
static int16_t mono[AV_AUDIO_SAMPLES];
static int32_t stereo[AV_AUDIO_SAMPLES * 2];
static audio_playout_t playout;

#include "wifi.inc"

static void play_block(const uint8_t *block)
{
    if (block) relay_audio_decode(block, mono, stereo);
    else memset(stereo, 0, sizeof stereo);
    for (size_t done = 0; done < AV_AUDIO_SAMPLES;) {
        size_t written = bsp_audio_write(stereo + done * 2, AV_AUDIO_SAMPLES - done, 45);
        if (!written) break;
        done += written;
    }
}

static void receive_volume(int fd, const struct sockaddr_in *peer, relay_volume_t volume)
{
    if (!relay_volume_matches(volume, last_volume)) {
        if (volume.generation == previous_generation || !relay_volume_newer(volume, last_volume)) return;
        if (bsp_audio_set_volume(volume.level) != ESP_OK) return;
        if (volume.generation != last_volume.generation) previous_generation = last_volume.generation;
        last_volume = volume;
        volume_percent = volume.level;
    }
    uint8_t ack[RELAY_VOLUME_BYTES];
    relay_volume_encode(ack, RELAY_VOLUME_ACK, volume);
    sendto(fd, ack, sizeof ack, MSG_DONTWAIT, (const struct sockaddr *)peer, sizeof *peer);
}

static void receive_audio(int fd, struct in_addr sender, unsigned session_link)
{
    memset(&playout, 0, sizeof playout);
    while (relay_link_current(&network_link, session_link)) {
        update_volume();
        uint8_t datagram[RELAY_AUDIO_BYTES + 1];
        struct sockaddr_in peer;
        socklen_t peer_size = sizeof peer;
        int n = recvfrom(fd, datagram, sizeof datagram, 0, (struct sockaddr *)&peer, &peer_size);
        if (!relay_link_current(&network_link, session_link)) return;
        const int64_t now = esp_timer_get_time();
        if (n > 0 && peer.sin_addr.s_addr == sender.s_addr) {
            relay_volume_t volume;
            relay_timing_t timing;
            if (relay_volume_decode(datagram, (size_t)n, RELAY_VOLUME_SET, &volume))
                receive_volume(fd, &peer, volume);
            else if (relay_timing_decode(datagram, (size_t)n, &timing)) {
                if (timing.delay_us == RELAY_TIMING_PROBE &&
                    audio_playout_timeline(&playout, timing.session, now, &timing.pts_ms, &timing.delay_us)) {
                    relay_timing_encode(datagram, timing);
                    sendto(fd, datagram, RELAY_TIMING_BYTES, MSG_DONTWAIT,
                           (const struct sockaddr *)&peer, sizeof peer);
                }
            } else {
                audio_playout_receive(&playout, datagram, (size_t)n, now);
            }
        }
        uint8_t block[AV_AUDIO_BYTES];
        const audio_due_t due = audio_playout_take(&playout, now, block);
        if (due != AUDIO_WAIT) play_block(due == AUDIO_BLOCK ? block : NULL);
    }
}

static bool load_defaults(speaker_config_t *settings)
{
    struct in_addr sender;
    if (!CONFIG_PASSPORT_TV_SSID[0] || strlen(CONFIG_PASSPORT_TV_SSID) >= sizeof settings->ssid ||
        strlen(CONFIG_PASSPORT_TV_PASSWORD) >= sizeof settings->password ||
        inet_pton(AF_INET, CONFIG_PASSPORT_TV_AUDIO_SENDER, &sender) != 1) return false;
    strlcpy(settings->ssid, CONFIG_PASSPORT_TV_SSID, sizeof settings->ssid);
    strlcpy(settings->password, CONFIG_PASSPORT_TV_PASSWORD, sizeof settings->password);
    memcpy(settings->sender, &sender.s_addr, sizeof settings->sender);
    settings->port = CONFIG_PASSPORT_TV_AUDIO_PORT;
    return true;
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(speaker_setup_start());
    speaker_config_t settings;
    if (!speaker_setup_load(&settings) && !load_defaults(&settings)) {
        ESP_LOGW(TAG, "Waiting for Wi-Fi and Indicator settings on USB");
        return;
    }
    struct in_addr sender;
    memcpy(&sender.s_addr, settings.sender, sizeof settings.sender);
    const uint16_t port = settings.port;
    ESP_ERROR_CHECK(bsp_audio_init());
    ESP_ERROR_CHECK(bsp_audio_set_volume(volume_percent));
    ESP_ERROR_CHECK(bsp_button_init(on_volume_button));
    start_wifi(&settings);
    memset(&settings, 0, sizeof settings);
    for (;;) {
        xEventGroupWaitBits(network, BIT0, pdFALSE, pdTRUE, portMAX_DELAY);
        const unsigned session_link = atomic_load(&network_link);
        if (!relay_link_current(&network_link, session_link)) continue;
        struct sockaddr_in local = {.sin_family = AF_INET,
                                   .sin_port = htons(port)};
        int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (fd >= 0) {
            const struct timeval timeout = {.tv_usec = 10000};
            setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
            if (bind(fd, (struct sockaddr *)&local, sizeof local) == 0) receive_audio(fd, sender, session_link);
            close(fd);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
