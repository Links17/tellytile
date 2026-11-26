#include "speaker_setup.h"
#include "driver/usb_serial_jtag.h"
#include "nvs.h"

#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

static const char *request;
static char response[128];
static size_t response_size;
static uint8_t flash[SPEAKER_RECORD_MAX], staged[SPEAKER_RECORD_MAX];
static size_t flash_size, staged_size;
static bool fail_commit, rebooted, transmitted;
static void (*setup_task)(void *);
static jmp_buf finished;

esp_err_t nvs_open(const char *name, nvs_open_mode_t mode, nvs_handle_t *h)
{
    assert(!strcmp(name, "tv_speaker"));
    *h = mode;
    return ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *data, size_t *size)
{
    assert(h == NVS_READONLY && !strcmp(key, "config"));
    if (!flash_size || *size < flash_size) return ESP_FAIL;
    memcpy(data, flash, flash_size);
    *size = flash_size;
    return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *data, size_t size)
{
    assert(h == NVS_READWRITE && !strcmp(key, "config") && size <= sizeof staged);
    memcpy(staged, data, size);
    staged_size = size;
    return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h)
{
    assert(h == NVS_READWRITE && staged_size);
    if (fail_commit) return ESP_FAIL;
    memcpy(flash, staged, staged_size);
    flash_size = staged_size;
    return ESP_OK;
}
void nvs_close(nvs_handle_t h) { (void)h; staged_size = 0; }

int xTaskCreate(void (*task)(void *), const char *name, unsigned stack, void *arg,
                unsigned prio, void *handle)
{
    (void)name; (void)stack; (void)arg; (void)prio; (void)handle;
    setup_task = task;
    return 1;
}
esp_err_t usb_serial_jtag_driver_install(usb_serial_jtag_driver_config_t *cfg)
{
    assert(cfg->rx_buffer_size >= SPEAKER_LINE_MAX);
    return ESP_OK;
}
void usb_serial_jtag_driver_uninstall(void) { assert(false); }
int usb_serial_jtag_read_bytes(void *data, uint32_t size, uint32_t timeout)
{
    (void)timeout;
    size_t n = strlen(request);
    if (!n) longjmp(finished, 1);
    if (n > 3) n = 3;
    assert(n <= size);
    memcpy(data, request, n);
    request += n;
    return (int)n;
}
int usb_serial_jtag_write_bytes(const void *data, size_t size, uint32_t timeout)
{
    (void)timeout;
    assert(response_size + size < sizeof response);
    memcpy(response + response_size, data, size);
    response_size += size;
    response[response_size] = 0;
    return (int)size;
}
esp_err_t usb_serial_jtag_wait_tx_done(uint32_t timeout)
{
    assert(timeout > 0);
    transmitted = true;
    return ESP_OK;
}
void esp_restart(void)
{
    assert(transmitted);
    rebooted = true;
    longjmp(finished, 1);
}

static void run(const char *next, const char *expected, bool restart)
{
    request = next;
    response_size = 0;
    rebooted = transmitted = false;
    if (!setjmp(finished)) setup_task(NULL);
    assert(strcmp(response, expected) == 0);
    assert(rebooted == restart);
}

int main(void)
{
    speaker_config_t config = {0};
    assert(speaker_setup_start() == ESP_OK && setup_task);
    assert(!speaker_setup_load(&config));
    run("TVSET ?\r\nTVSET1 abc\r\n", "TVSET READY\nTVSET INVALID\n", false);
    assert(!speaker_setup_load(&config));
    run("TVSET1 545653310408c000021546b0666f6c6f73796e7468657469\n", "TVSET SAVED\n", true);
    assert(speaker_setup_load(&config));
    assert(!strcmp(config.ssid, "folo") && !strcmp(config.password, "syntheti"));
    assert(config.sender[0] == 192 && config.sender[3] == 21 && config.port == 18096);
    fail_commit = true;
    run("TVSET1 545653310400c000021646b16e657874\n", "TVSET STORE_FAILED\n", false);
    assert(speaker_setup_load(&config) && !strcmp(config.ssid, "folo"));
    fail_commit = false;
    run("TVSET1 545653310400c000021646b16e657874\n", "TVSET SAVED\n", true);
    assert(speaker_setup_load(&config));
    assert(!strcmp(config.ssid, "next") && !config.password[0] && config.port == 18097);
    puts("USB setup passed split commands, invalid input, NVS round trip, atomic update failure and reboot order.");
}
