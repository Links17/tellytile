# Copyright (c) 2026 CodeCraft AI Hardware contributors.
# SPDX-License-Identifier: Apache-2.0
# Native composition for this example; no implicit all-peripheral fallback.
set(CODECRAFT_BSP_SOURCES
    "src/bsp_button.c"
    "src/bsp_display.c"
    "src/bsp_io_expander.c"
    "src/bsp_touch.c"
)
set(CODECRAFT_BSP_REQUIRES
    esp_driver_gpio
    esp_driver_i2c
    esp_lcd
    esp_lcd_touch_ft5x06
    esp_lcd_touch_gt911
    freertos
    log
)
