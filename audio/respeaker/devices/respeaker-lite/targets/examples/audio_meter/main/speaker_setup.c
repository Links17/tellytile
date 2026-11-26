#include "speaker_setup.h"

#include "driver/usb_serial_jtag.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include <string.h>

static const char name[] = "tv_speaker";
static const char key[] = "config";

bool speaker_setup_load(speaker_config_t *config)
{
    nvs_handle_t handle;
    if (nvs_open(name, NVS_READONLY, &handle) != ESP_OK) return false;
    uint8_t record[SPEAKER_RECORD_MAX];
    size_t size = sizeof record;
    bool loaded = nvs_get_blob(handle, key, record, &size) == ESP_OK &&
                  speaker_config_decode(record, size, config);
    memset(record, 0, sizeof record);
    nvs_close(handle);
    return loaded;
}

static esp_err_t save(const uint8_t *record, size_t size)
{
    nvs_handle_t handle;
    esp_err_t status = nvs_open(name, NVS_READWRITE, &handle);
    if (status != ESP_OK) return status;
    status = nvs_set_blob(handle, key, record, size);
    if (status == ESP_OK) status = nvs_commit(handle);
    nvs_close(handle);
    return status;
}

static void reply(const char *text)
{
    usb_serial_jtag_write_bytes(text, strlen(text), pdMS_TO_TICKS(500));
}

static void process(const char *line, size_t length)
{
    if (length == 7 && !memcmp(line, "TVSET ?", 7)) {
        reply("TVSET READY\n");
        return;
    }
    uint8_t record[SPEAKER_RECORD_MAX];
    const size_t size = speaker_config_command(line, length, record);
    if (!size) {
        memset(record, 0, sizeof record);
        reply("TVSET INVALID\n");
        return;
    }
    const esp_err_t status = save(record, size);
    memset(record, 0, sizeof record);
    if (status != ESP_OK) {
        reply("TVSET STORE_FAILED\n");
        return;
    }
    reply("TVSET SAVED\n");
    usb_serial_jtag_wait_tx_done(pdMS_TO_TICKS(500));
    esp_restart();
}

static void setup_task(void *arg)
{
    (void)arg;
    char line[SPEAKER_LINE_MAX];
    size_t length = 0;
    bool overflow = false;
    for (;;) {
        uint8_t bytes[64];
        const int count = usb_serial_jtag_read_bytes(bytes, sizeof bytes, pdMS_TO_TICKS(100));
        for (int i = 0; i < count; ++i) {
            const uint8_t ch = bytes[i];
            if (ch == '\n' || ch == '\r') {
                if (overflow) reply("TVSET INVALID\n");
                else if (length) process(line, length);
                memset(line, 0, length);
                length = 0;
                overflow = false;
            } else if (length == sizeof line || ch < 32 || ch > 126) {
                overflow = true;
            } else if (!overflow) {
                line[length++] = ch;
            }
        }
    }
}

esp_err_t speaker_setup_start(void)
{
    usb_serial_jtag_driver_config_t usb = {.rx_buffer_size = 512, .tx_buffer_size = 256};
    esp_err_t status = usb_serial_jtag_driver_install(&usb);
    if (status != ESP_OK) return status;
    if (xTaskCreate(setup_task, "tv_speaker_setup", 4096, NULL, 3, NULL) == pdPASS) return ESP_OK;
    usb_serial_jtag_driver_uninstall();
    return ESP_ERR_NO_MEM;
}
