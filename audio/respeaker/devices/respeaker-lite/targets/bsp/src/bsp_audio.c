/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 * Modified 2026 by CodeCraft AI Hardware contributors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Derived from the imported hardware reference main/voice_board_respeaker_lite.c.
 * I2S reference: https://wiki.seeedstudio.com/respeaker_record_and_play/
 * Requires the vendor's 16 kHz I2S XMOS firmware, 32-bit stereo slots.
 */
#include "bsp_audio.h"
#include "bsp_pins.h"

#include <limits.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "bsp_audio";

#define BSP_I2S_CHUNK_FRAMES 240

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_xmos, s_codec;
static i2s_chan_handle_t s_rx, s_tx;
static SemaphoreHandle_t s_lock;
static bool s_mic_on;
static bool s_mic_fault;
static bool s_tx_enabled;
static bool s_ready;

static esp_err_t control_lock(void)
{
    TickType_t wait = pdMS_TO_TICKS(100);
    return xSemaphoreTake(s_lock, wait ? wait : 1) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

static esp_err_t xmos_read(uint8_t resource, uint8_t command, uint8_t *out, size_t len)
{
    uint8_t req[] = {resource, (uint8_t)(command | 0x80), (uint8_t)(len + 1)};
    uint8_t resp[4];
    ESP_RETURN_ON_FALSE(len <= 3, ESP_ERR_INVALID_SIZE, TAG, "xmos_read len %u", (unsigned)len);
    ESP_RETURN_ON_ERROR(control_lock(), TAG, "control mutex");
    /* Seeed's Wire.endTransmission() sends STOP before requestFrom(). XMOS
     * processes the command between these distinct transactions. */
    esp_err_t err = i2c_master_transmit(s_xmos, req, sizeof(req), 100);
    if (err == ESP_OK) err = i2c_master_receive(s_xmos, resp, len + 1, 100);
    xSemaphoreGive(s_lock);
    ESP_RETURN_ON_ERROR(err, TAG, "xmos_read: %s", esp_err_to_name(err));
    ESP_RETURN_ON_FALSE(resp[0] == 0, ESP_ERR_INVALID_RESPONSE, TAG, "xmos status %u", resp[0]);
    for (size_t i = 0; i < len; i++) out[i] = resp[i + 1];
    return ESP_OK;
}

static esp_err_t set_volume(int percent)
{
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    /* Digital gain -63.5 dB to 0 dB. Never boost above unity. */
    uint8_t page[] = {0, 0};
    uint8_t left[] = {0x41, (uint8_t)(int8_t)(-127 + percent * 127 / 100)};
    uint8_t right[] = {0x42, left[1]};
    uint8_t mute[] = {0x40, percent ? 0 : 0x0c};
    ESP_RETURN_ON_ERROR(control_lock(), TAG, "control mutex");
    esp_err_t err = i2c_master_transmit(s_codec, page, sizeof(page), 100);
    if (err == ESP_OK) err = i2c_master_transmit(s_codec, left, sizeof(left), 100);
    if (err == ESP_OK) err = i2c_master_transmit(s_codec, right, sizeof(right), 100);
    if (err == ESP_OK) err = i2c_master_transmit(s_codec, mute, sizeof(mute), 100);
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t bsp_audio_set_volume(int percent)
{
    if (!s_ready) return ESP_ERR_INVALID_STATE;
    return set_volume(percent);
}

bool bsp_audio_muted(void)
{
    uint8_t muted = 1;
    /* Firmware >= 1.0.9 exposes the hardware mute state. Fail closed. */
    return !s_ready || xmos_read(0xf1, 0x01, &muted, 1) != ESP_OK || muted != 0;
}

static esp_err_t audio_cleanup(void)
{
    /* Only called during failed serialized startup: no channel/bus has been
     * published to workers. Stop before deleting, and retain uncertain owned
     * resources for reset rather than overwrite them during another init. */
    if (s_tx_enabled) {
        esp_err_t ret = i2s_channel_disable(s_tx);
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) return ret;
        s_tx_enabled = false;
    }
    if (s_rx) {
        ESP_RETURN_ON_ERROR(i2s_del_channel(s_rx), TAG, "delete RX");
        s_rx = NULL;
    }
    if (s_tx) {
        ESP_RETURN_ON_ERROR(i2s_del_channel(s_tx), TAG, "delete TX");
        s_tx = NULL;
    }
    if (s_codec) {
        ESP_RETURN_ON_ERROR(i2c_master_bus_rm_device(s_codec), TAG, "delete codec");
        s_codec = NULL;
    }
    if (s_xmos) {
        ESP_RETURN_ON_ERROR(i2c_master_bus_rm_device(s_xmos), TAG, "delete XMOS");
        s_xmos = NULL;
    }
    if (s_bus) {
        ESP_RETURN_ON_ERROR(i2c_del_master_bus(s_bus), TAG, "delete bus");
        s_bus = NULL;
    }
    if (s_lock) {
        vSemaphoreDelete(s_lock);
        s_lock = NULL;
    }
    return ESP_OK;
}

esp_err_t bsp_audio_init(void)
{
    if (s_ready) return ESP_OK;
    if (s_lock || s_bus || s_xmos || s_codec || s_rx || s_tx) return ESP_ERR_INVALID_STATE;
    s_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_lock, ESP_ERR_NO_MEM, TAG, "mutex");

    i2c_master_bus_config_t bus = {
        .i2c_port = 0, .sda_io_num = BSP_PIN_I2C_SDA, .scl_io_num = BSP_PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7,
    };
    esp_err_t ret;
    ESP_GOTO_ON_ERROR(i2c_new_master_bus(&bus, &s_bus), fail, TAG, "i2c bus");

    i2c_device_config_t dev = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = BSP_I2C_ADDR_XMOS, .scl_speed_hz = BSP_I2C_FREQ_HZ,
    };
    esp_err_t err = i2c_master_bus_add_device(s_bus, &dev, &s_xmos);
    dev.device_address = BSP_I2C_ADDR_CODEC;
    if (err == ESP_OK) err = i2c_master_bus_add_device(s_bus, &dev, &s_codec);
    ESP_GOTO_ON_ERROR(err, fail, TAG, "i2c devices");

    gpio_config_t reset = {.pin_bit_mask = 1ULL << BSP_PIN_XMOS_RESET, .mode = GPIO_MODE_OUTPUT};
    err = gpio_config(&reset);
    ESP_GOTO_ON_ERROR(err, fail, TAG, "xmos reset gpio");
    ESP_GOTO_ON_ERROR(gpio_set_level(BSP_PIN_XMOS_RESET, 1), fail, TAG, "reset high");
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_GOTO_ON_ERROR(gpio_set_level(BSP_PIN_XMOS_RESET, 0), fail, TAG, "reset low");
    vTaskDelay(pdMS_TO_TICKS(3000));

    uint8_t version[3];
    err = xmos_read(0xf0, 0x58, version, 3);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "XMOS not answering (%s): requires Seeed 16 kHz I2S firmware", esp_err_to_name(err));
        ret = err;
        goto fail;
    }
    ESP_LOGI(TAG, "XMOS firmware %u.%u.%u; expected 16 kHz I2S variant",
             version[0], version[1], version[2]);

    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(BSP_I2S_PORT, I2S_ROLE_SLAVE);
    chan.dma_desc_num = 6;
    chan.dma_frame_num = BSP_I2S_CHUNK_FRAMES;
    chan.auto_clear = true;
    err = i2s_new_channel(&chan, &s_tx, &s_rx);
    i2s_std_config_t cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(BSP_I2S_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED, .bclk = BSP_PIN_I2S_BCLK, .ws = BSP_PIN_I2S_WS,
            .dout = BSP_PIN_I2S_DOUT, .din = BSP_PIN_I2S_DIN,
        },
    };
    if (err == ESP_OK) err = i2s_channel_init_std_mode(s_tx, &cfg);
    if (err == ESP_OK) err = i2s_channel_init_std_mode(s_rx, &cfg);
    if (err == ESP_OK) {
        err = i2s_channel_enable(s_tx);
        if (err == ESP_OK) s_tx_enabled = true;
    }
    ESP_GOTO_ON_ERROR(err, fail, TAG, "i2s init");

    ESP_GOTO_ON_ERROR(set_volume(60), fail, TAG, "initial volume");
    s_ready = true;
    ESP_LOGI(TAG, "reSpeaker audio ready; XIAO BOOT is push-to-talk");
    return ESP_OK;

fail:
    if (audio_cleanup() != ESP_OK) ESP_LOGE(TAG, "audio rollback incomplete; reset required");
    return ret;
}

esp_err_t bsp_audio_mic_start(void)
{
    if (!s_ready || s_mic_fault) return ESP_ERR_INVALID_STATE;
    if (s_mic_on) return ESP_OK;
    esp_err_t err = i2s_channel_enable(s_rx);
    if (err == ESP_OK) s_mic_on = true;
    return err;
}

void bsp_audio_mic_stop(void)
{
    if (!s_ready || !s_mic_on) return;
    esp_err_t ret = i2s_channel_disable(s_rx);
    if (ret == ESP_OK || ret == ESP_ERR_INVALID_STATE) {
        s_mic_on = false;
        s_mic_fault = false;
    } else {
        /* The public API is void. Keep ownership, block read/start, and let
         * the RX-owning task retry stop instead of claiming it succeeded. */
        s_mic_fault = true;
        ESP_LOGE(TAG, "mic stop failed (%s); retry stop before restarting", esp_err_to_name(ret));
    }
}

size_t bsp_audio_mic_read(int32_t *stereo_out, size_t frames, uint32_t timeout_ms)
{
    if (!s_ready || !s_mic_on || s_mic_fault || !stereo_out ||
        frames == 0 || frames > INT_MAX / (2 * sizeof(*stereo_out))) return 0;
    size_t bytes = 0;
    (void)i2s_channel_read(s_rx, stereo_out, frames * 2 * sizeof(*stereo_out), &bytes,
                           timeout_ms);
    return bytes / (2 * sizeof(int32_t));
}

size_t bsp_audio_write(const int32_t *stereo_in, size_t frames, uint32_t timeout_ms)
{
    if (!s_ready || !stereo_in || frames == 0 ||
        frames > INT_MAX / (2 * sizeof(*stereo_in))) return 0;
    size_t written = 0;
    (void)i2s_channel_write(s_tx, stereo_in, frames * 2 * sizeof(*stereo_in), &written,
                            timeout_ms);
    return written / (2 * sizeof(int32_t));
}
