/* SenseCAP Indicator display: ST7701S 480x480 RGB panel.
 * The panel's register channel is a bit-banged 9-bit 3-wire SPI (SCK/SDO on
 * GPIO, chip select on the TCA9535 expander); pixels stream over the 16-bit
 * RGB bus through esp_lcd. Framebuffer lives in octal PSRAM. */
#pragma once

#include "esp_err.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Task context only. One display-owning task or an application mutex must
 * serialize init, backlight access and all operations on the borrowed panel.
 * No ISR or non-blocking button/UI callback calls. LVGL calls still need the
 * application's LVGL lock. Native RGB interrupt callbacks must not block.
 * Initialize during serialized board startup, before touch consumers;
 * no other task may change the LCD CS/reset or register-channel GPIOs.
 *
 * Initializes the IO expander, leaves the unused RP2040 in reset, releases
 * the touch reset, creates the esp_lcd RGB panel (480x480, RGB565, framebuffer in PSRAM) and runs the
 * ST7701S register init for the GX screen variant. The native DX build profile
 * instead releases the vendor-defined LCD CS/reset outputs without register
 * writes. The selected profile is not physical panel/revision detection;
 * see devices/sensecap-indicator/reference/display-variants.md.
 * out_panel may be NULL; otherwise it is cleared on failure. Repeated success
 * returns the same panel without pulsing shared reset or reprogramming it.
 * Only a fully initialized panel is published. Failed init deletes its RGB
 * driver; complete cleanup allows retry before other reset consumers start.
 * Incomplete cleanup blocks reuse until reset. Shared bus/rails stay owned
 * by the board; their state and panel contents are uncertain after errors.
 * Successful panel/framebuffer allocations live until reset. Do not delete
 * or independently reset the borrowed panel, expander or bus.
 *
 * Each expander access has its documented mutex/I2C wait; vendor fixed delays
 * and bit-bang GPIO waits add to init. No hard total deadline is promised.
 * In this IDF v6.0.2 single-framebuffer configuration, draw_bitmap copies an
 * external caller buffer synchronously into the RGB framebuffer. Keep that
 * buffer immutable through the call. A pointer obtained from the native RGB
 * framebuffer API is instead read continuously into the internal scanout
 * buffers; it is borrowed and must never be freed. Framebuffer updates may
 * tear; transfer/copy completion
 * does not prove visible frame completion. No physical recovery is verified. */
esp_err_t bsp_display_init(esp_lcd_panel_handle_t *out_panel);

/* Backlight on GPIO 45, active high; requires ready state. GPIO errors
 * propagate and leave physical state uncertain. Getter returns only the
 * last successful requested value (false initially), not a measurement. */
esp_err_t bsp_display_backlight_set(bool on);

bool bsp_display_backlight_get(void);

#ifdef __cplusplus
}
#endif
