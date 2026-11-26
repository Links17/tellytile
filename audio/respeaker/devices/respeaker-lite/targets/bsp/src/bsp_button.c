/* reSpeaker Lite button: XIAO BOOT on GPIO 0, active low. Polled with
 * debounce in a dedicated task; callbacks must stay non-blocking. */
#include "bsp_button.h"
#include "bsp_pins.h"

#include <stdatomic.h>

#include "driver/gpio.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define BSP_BUTTON_DEBOUNCE_MS 30
#define BSP_BUTTON_POLL_MS     20

static bool s_started;
static bsp_button_cb_t s_cb;
static atomic_bool s_pressed;

static void bsp_button_task(void *arg)
{
    (void)arg;
    bool stable = false, raw_last = false;
    uint32_t stable_ms = 0;
    for (;;) {
        bool raw = gpio_get_level(BSP_PIN_BUTTON_BOOT) == 0;
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
        if (raw != raw_last) { raw_last = raw; stable_ms = now; }
        if (raw != stable && now - stable_ms >= BSP_BUTTON_DEBOUNCE_MS) {
            stable = raw;
            atomic_store(&s_pressed, stable);
            if (s_cb) s_cb(stable, NULL);
        }
        TickType_t wait = pdMS_TO_TICKS(BSP_BUTTON_POLL_MS);
        vTaskDelay(wait ? wait : 1);
    }
}

esp_err_t bsp_button_init(bsp_button_cb_t cb)
{
    if (s_started) return s_cb == cb ? ESP_OK : ESP_ERR_INVALID_STATE;
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << BSP_PIN_BUTTON_BOOT,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&cfg), "bsp_button", "gpio");
    s_cb = cb;
    if (xTaskCreate(bsp_button_task, "bsp_button", 2048, NULL, 5, NULL) != pdPASS) {
        s_cb = NULL;
        return ESP_ERR_NO_MEM;
    }
    s_started = true;
    return ESP_OK;
}

bool bsp_button_pressed(void)
{
    return atomic_load(&s_pressed);
}
