/* reSpeaker Lite status LED: one WS2812 on GPIO 1 (XIAO D0). */
#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Task context only. One LED-owning task or application mutex serializes
 * init/set/off; no ISR or non-blocking callback calls. The BSP owns its
 * native RMT strip and buffers until reset. Init failure leaves it unready;
 * native creation owns partial cleanup and a BSP retry is supported. SDK
 * cleanup errors hidden inside that constructor are not recoverable here.
 * Repeated success reuses the same strip without resetting the color.
 * Native refresh/clear waits indefinitely for RMT completion (-1); there
 * is no BSP-imposed deadline. Color/clear errors propagate and leave visible
 * state uncertain; retry the complete desired color or off request. */
esp_err_t bsp_led_init(void);
esp_err_t bsp_led_set_rgb(uint8_t r, uint8_t g, uint8_t b);
esp_err_t bsp_led_off(void);

#ifdef __cplusplus
}
#endif
