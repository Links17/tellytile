/* ST7701S 480x480 RGB panel driver for the SenseCAP Indicator.
 *
 * Hardware facts: Seeed-Solution/SenseCAP_Indicator_ESP32 (Apache 2.0) —
 * components/bsp/src/boards/sensecap_indicator_board.c (pin map, expander
 * assignments) and components/bsp/src/boards/lcd_panel_config.c
 * (lcd_panel_st7701s_init register sequence, 9-bit SPI bit-bang timings,
 * 18 MHz PCLK, porch/width timing for the GX screen).
 *
 * The register sequence below is the vendor's functional init table,
 * transcribed into a data-driven form; see the vendor file for the original.
 */
#include "bsp_display.h"
#include "bsp_io_expander.h"
#include "bsp_pins.h"

#include <string.h>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* Native profiles select a documented assembly, not a detected PCB revision.
 * Source: devices/sensecap-indicator/reference/display-variants.md, pinned
 * vendor sensecap_indicator_board.c GX/DX branches. */
#if !defined(CODECRAFT_DISPLAY_GX) || (CODECRAFT_DISPLAY_GX != 0 && CODECRAFT_DISPLAY_GX != 1)
#error "Select the documented GX or DX native display profile"
#endif

static const char *TAG = "bsp_display";

static esp_lcd_panel_handle_t s_panel;
static bool s_backlight;
static bool s_ready;

/* ---- 9-bit 3-wire SPI (panel register channel) ----
 * SCK/SDO are plain GPIOs, CS sits on the TCA9535 expander. The vendor
 * bit-bangs at ~50 kHz (10 us half-period); keep the same timing. */
#if CODECRAFT_DISPLAY_GX

static esp_err_t spi_set_sck(int level)
{
    ESP_RETURN_ON_ERROR(gpio_set_level(BSP_PIN_LCD_SPI_SCK, level), TAG, "SCK");
    esp_rom_delay_us(10);
    return ESP_OK;
}

static esp_err_t spi_set_sdo(int level)
{
    return gpio_set_level(BSP_PIN_LCD_SPI_SDO, level);
}

static esp_err_t spi_set_cs(int level)
{
    ESP_RETURN_ON_ERROR(bsp_io_expander_set_level(BSP_EXP_IO_LCD_CS, level), TAG, "CS");
    esp_rom_delay_us(10);
    return ESP_OK;
}

/* Send 9 bits, MSB first: bit 8 is the data/command select. */
static esp_err_t spi_send9(uint16_t value)
{
    for (int n = 0; n < 9; n++) {
        ESP_RETURN_ON_ERROR(spi_set_sdo(value & 0x100), TAG, "SDO");
        value <<= 1;
        ESP_RETURN_ON_ERROR(spi_set_sck(1), TAG, "SCK high");
        ESP_RETURN_ON_ERROR(spi_set_sck(0), TAG, "SCK low");
    }
    return ESP_OK;
}

/* Vendor writes a 16-bit command word as two 9-bit transfers: a leading
 * zero frame, then the command byte (both with DC = command). */
static esp_err_t st7701s_write_cmd(uint8_t cmd)
{
    ESP_RETURN_ON_ERROR(spi_set_cs(0), TAG, "CS low");
    ESP_RETURN_ON_ERROR(spi_set_sck(0), TAG, "SCK low");
    ESP_RETURN_ON_ERROR(spi_send9(0x000), TAG, "command prefix");
    ESP_RETURN_ON_ERROR(spi_set_sck(1), TAG, "SCK high");
    ESP_RETURN_ON_ERROR(spi_set_sck(0), TAG, "SCK low");
    ESP_RETURN_ON_ERROR(spi_set_cs(1), TAG, "CS high");
    ESP_RETURN_ON_ERROR(spi_set_cs(0), TAG, "CS low");
    ESP_RETURN_ON_ERROR(spi_send9(cmd), TAG, "command");
    return spi_set_cs(1);
}

static esp_err_t st7701s_write_data(uint8_t data)
{
    ESP_RETURN_ON_ERROR(spi_set_cs(0), TAG, "CS low");
    ESP_RETURN_ON_ERROR(spi_set_sck(0), TAG, "SCK low");
    ESP_RETURN_ON_ERROR(spi_send9(0x100 | data), TAG, "data");
    ESP_RETURN_ON_ERROR(spi_set_sck(1), TAG, "SCK high");
    ESP_RETURN_ON_ERROR(spi_set_sck(0), TAG, "SCK low");
    return spi_set_cs(1);
}

typedef struct {
    uint8_t cmd;
    uint8_t data[16];
    uint8_t len;            /* 0xFF marks a delay instead of a write */
    uint8_t delay_ms;
} st7701s_op_t;

/* GX screen init, transcribed from the vendor's lcd_panel_st7701s_init(). */
static const st7701s_op_t s_st7701s_init[] = {
    {0xFF, {0x77, 0x01, 0x00, 0x00, 0x10}, 5, 0},
    {0xC0, {0x3B, 0x00}, 2, 0},                       /* 480x480 */
    {0xC1, {0x0D, 0x02}, 2, 0},
    {0xC2, {0x31, 0x05}, 2, 0},
    {0xC7, {0x04}, 1, 0},
    {0xCD, {0x08}, 1, 0},
    {0xB0, {0x00, 0x11, 0x18, 0x0E, 0x11, 0x06, 0x07, 0x08,
            0x07, 0x22, 0x04, 0x12, 0x0F, 0xAA, 0x31, 0x18}, 16, 0},
    {0xB1, {0x00, 0x11, 0x19, 0x0E, 0x12, 0x07, 0x08, 0x08,
            0x08, 0x22, 0x04, 0x11, 0x11, 0xA9, 0x32, 0x18}, 16, 0},
    {0xFF, {0x77, 0x01, 0x00, 0x00, 0x11}, 5, 0},
    {0xB0, {0x60}, 1, 0},
    {0xB1, {0x32}, 1, 0},
    {0xB2, {0x07}, 1, 0},
    {0xB3, {0x80}, 1, 0},
    {0xB5, {0x49}, 1, 0},
    {0xB7, {0x85}, 1, 0},
    {0xB8, {0x21}, 1, 0},
    {0xC1, {0x78}, 1, 0},
    {0xC2, {0x78}, 1, 20},
    {0xE0, {0x00, 0x1B, 0x02}, 3, 0},
    {0xE1, {0x08, 0xA0, 0x00, 0x00, 0x07, 0xA0, 0x00, 0x00,
            0x00, 0x44, 0x44}, 11, 0},
    {0xE2, {0x11, 0x11, 0x44, 0x44, 0xED, 0xA0, 0x00, 0x00,
            0xEC, 0xA0, 0x00, 0x00}, 12, 0},
    {0xE3, {0x00, 0x00, 0x11, 0x11}, 4, 0},
    {0xE4, {0x44, 0x44}, 2, 0},
    {0xE5, {0x0A, 0xE9, 0xD8, 0xA0, 0x0C, 0xEB, 0xD8, 0xA0,
            0x0E, 0xED, 0xD8, 0xA0, 0x10, 0xEF, 0xD8, 0xA0}, 16, 0},
    {0xE6, {0x00, 0x00, 0x11, 0x11}, 4, 0},
    {0xE7, {0x44, 0x44}, 2, 0},
    {0xE8, {0x09, 0xE8, 0xD8, 0xA0, 0x0B, 0xEA, 0xD8, 0xA0,
            0x0D, 0xEC, 0xD8, 0xA0, 0x0F, 0xEE, 0xD8, 0xA0}, 16, 0},
    {0xEB, {0x02, 0x00, 0xE4, 0xE4, 0x88, 0x00, 0x40}, 7, 0},
    {0xEC, {0x3C, 0x00}, 2, 0},
    {0xED, {0xAB, 0x89, 0x76, 0x54, 0x02, 0xFF, 0xFF, 0xFF,
            0xFF, 0xFF, 0xFF, 0x20, 0x45, 0x67, 0x98, 0xBA}, 16, 0},
    {0x36, {0x10}, 1, 0},
    {0xFF, {0x77, 0x01, 0x00, 0x00, 0x13}, 5, 0},
    {0xE5, {0xE4}, 1, 0},
    {0xFF, {0x77, 0x01, 0x00, 0x00, 0x00}, 5, 0},
    {0x3A, {0x60}, 1, 0},                             /* RGB666 */
    {0x21, {0}, 0, 0},                                /* inversion on */
    {0x11, {0}, 0, 120},                              /* sleep out */
    {0x29, {0}, 0, 120},                              /* display on */
};

static esp_err_t st7701s_init(void)
{
    /* CS and reset lines on the expander: idle high, both outputs. */
    ESP_RETURN_ON_ERROR(bsp_io_expander_set_level(BSP_EXP_IO_LCD_CS, 1), TAG,
                        "lcd cs");
    ESP_RETURN_ON_ERROR(bsp_io_expander_set_level(BSP_EXP_IO_LCD_RESET, 1),
                        TAG, "lcd reset");
    ESP_RETURN_ON_ERROR(bsp_io_expander_set_direction(BSP_EXP_IO_LCD_CS, 1),
                        TAG, "lcd cs dir");
    ESP_RETURN_ON_ERROR(bsp_io_expander_set_direction(BSP_EXP_IO_LCD_RESET, 1),
                        TAG, "lcd reset dir");

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BSP_PIN_LCD_SPI_SCK) |
                        (1ULL << BSP_PIN_LCD_SPI_SDO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io_conf), TAG, "spi gpio");

    for (size_t i = 0; i < sizeof(s_st7701s_init) / sizeof(s_st7701s_init[0]);
         i++) {
        const st7701s_op_t *op = &s_st7701s_init[i];
        ESP_RETURN_ON_ERROR(st7701s_write_cmd(op->cmd), TAG, "vendor command");
        for (int d = 0; d < op->len; d++) {
            ESP_RETURN_ON_ERROR(st7701s_write_data(op->data[d]), TAG, "vendor data");
        }
        if (op->delay_ms) {
            vTaskDelay(pdMS_TO_TICKS(op->delay_ms));
        }
    }

    /* Leave the register channel idle-high, per the vendor sequence. */
    ESP_RETURN_ON_ERROR(spi_set_cs(1), TAG, "idle CS");
    ESP_RETURN_ON_ERROR(gpio_set_level(BSP_PIN_LCD_SPI_SCK, 1), TAG, "idle SCK");
    ESP_RETURN_ON_ERROR(gpio_set_level(BSP_PIN_LCD_SPI_SDO, 1), TAG, "idle SDO");
    return ESP_OK;
}

#endif /* CODECRAFT_DISPLAY_GX: DX does not send ST7701S register writes. */

static esp_err_t rgb_panel_create(esp_lcd_panel_handle_t *out_panel)
{
    esp_lcd_rgb_panel_config_t panel_config = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .data_width = 16,
        /* Internal DMA buffers absorb brief PSRAM contention during playback. */
        .bounce_buffer_size_px = 10 * BSP_LCD_H_RES,
        .disp_gpio_num = GPIO_NUM_NC,
        .pclk_gpio_num = BSP_PIN_LCD_PCLK,
        .vsync_gpio_num = BSP_PIN_LCD_VSYNC,
        .hsync_gpio_num = BSP_PIN_LCD_HSYNC,
        .de_gpio_num = BSP_PIN_LCD_DE,
        .data_gpio_nums = {
            BSP_PIN_LCD_D0,  BSP_PIN_LCD_D1,  BSP_PIN_LCD_D2,
            BSP_PIN_LCD_D3,  BSP_PIN_LCD_D4,  BSP_PIN_LCD_D5,
            BSP_PIN_LCD_D6,  BSP_PIN_LCD_D7,  BSP_PIN_LCD_D8,
            BSP_PIN_LCD_D9,  BSP_PIN_LCD_D10, BSP_PIN_LCD_D11,
            BSP_PIN_LCD_D12, BSP_PIN_LCD_D13, BSP_PIN_LCD_D14,
            BSP_PIN_LCD_D15,
        },
        .timings = {
            /* 16 MHz gives GX about 56 Hz with the vendor porch timings. */
            .pclk_hz = CODECRAFT_DISPLAY_GX ? 16000000 : BSP_LCD_PCLK_HZ,
            .h_res = BSP_LCD_H_RES,
            .v_res = BSP_LCD_V_RES,
            .hsync_pulse_width = BSP_LCD_HSYNC_PULSE_WIDTH,
            .hsync_back_porch = BSP_LCD_HSYNC_BACK_PORCH,
            .hsync_front_porch = BSP_LCD_HSYNC_FRONT_PORCH,
            .vsync_pulse_width = BSP_LCD_VSYNC_PULSE_WIDTH,
            .vsync_back_porch = BSP_LCD_VSYNC_BACK_PORCH,
            .vsync_front_porch = BSP_LCD_VSYNC_FRONT_PORCH,
            .flags.pclk_active_neg = BSP_LCD_PCLK_ACTIVE_NEG,
        },
        .flags.fb_in_psram = 1,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_rgb_panel(&panel_config, out_panel), TAG,
                        "rgb panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(*out_panel), TAG, "panel reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(*out_panel), TAG, "panel init");
    return ESP_OK;
}

esp_err_t bsp_display_init(esp_lcd_panel_handle_t *out_panel)
{
    if (out_panel) *out_panel = NULL;
    if (s_ready) {
        if (out_panel) {
            *out_panel = s_panel;
        }
        return ESP_OK;
    }

    ESP_RETURN_ON_FALSE(!s_panel, ESP_ERR_INVALID_STATE, TAG,
                        "incomplete rollback; reset required");
    ESP_RETURN_ON_ERROR(bsp_io_expander_init(), TAG, "io expander");

    /* FoloTV has no RP2040 clients. Keep it in reset to silence its buzzer.
     * Load the active-low reset level before enabling the expander output. */
    ESP_RETURN_ON_ERROR(bsp_io_expander_set_level(BSP_EXP_IO_RP2040_RESET, 0),
                        TAG, "rp2040 reset level");
    ESP_RETURN_ON_ERROR(bsp_io_expander_set_direction(BSP_EXP_IO_RP2040_RESET, 1),
                        TAG, "rp2040 reset dir");
    ESP_LOGI(TAG, "RP2040 held in reset");

    /* Pulse touch reset and power the BMP sensor rail. */
    ESP_RETURN_ON_ERROR(bsp_io_expander_set_level(BSP_EXP_IO_TP_RESET, 0), TAG,
                        "tp reset");
    ESP_RETURN_ON_ERROR(bsp_io_expander_set_direction(BSP_EXP_IO_TP_RESET, 1),
                        TAG, "tp reset dir");
    /* Preserve the vendor's 5 ms pulse even with a 100 Hz RTOS tick. */
    esp_rom_delay_us(5000);
    ESP_RETURN_ON_ERROR(bsp_io_expander_set_level(BSP_EXP_IO_TP_RESET, 1), TAG,
                        "tp release");
    ESP_RETURN_ON_ERROR(bsp_io_expander_set_direction(BSP_EXP_IO_BMP_PWR, 1),
                        TAG, "bmp pwr dir");
    ESP_RETURN_ON_ERROR(bsp_io_expander_set_level(BSP_EXP_IO_BMP_PWR, 1), TAG,
                        "bmp pwr on");

    /* Vendor order: RGB streaming first, then the ST7701S register init. */
    esp_err_t ret;
    ESP_GOTO_ON_ERROR(rgb_panel_create(&s_panel), fail, TAG, "rgb panel");
#if CODECRAFT_DISPLAY_GX
    ESP_GOTO_ON_ERROR(st7701s_init(), fail, TAG, "st7701s init");
#else
    /* Vendor DX branch: release LCD CS/reset, then enable both outputs.
     * Same pinned source as above; no register-channel GPIO writes. */
    ESP_GOTO_ON_ERROR(bsp_io_expander_set_level(BSP_EXP_IO_LCD_CS, 1), fail, TAG, "lcd cs");
    ESP_GOTO_ON_ERROR(bsp_io_expander_set_level(BSP_EXP_IO_LCD_RESET, 1), fail, TAG, "lcd reset");
    ESP_GOTO_ON_ERROR(bsp_io_expander_set_direction(BSP_EXP_IO_LCD_CS, 1), fail, TAG, "lcd cs dir");
    ESP_GOTO_ON_ERROR(bsp_io_expander_set_direction(BSP_EXP_IO_LCD_RESET, 1), fail, TAG, "lcd reset dir");
#endif

    gpio_config_t bl_conf = {
        .pin_bit_mask = 1ULL << BSP_PIN_LCD_BACKLIGHT,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_GOTO_ON_ERROR(gpio_config(&bl_conf), fail, TAG, "backlight gpio");
    s_ready = true;

    if (out_panel) {
        *out_panel = s_panel;
    }
    ESP_LOGI(TAG, "480x480 RGB panel ready (%s build profile)", CODECRAFT_DISPLAY_GX ? "GX" : "DX");
    return ESP_OK;

fail:
    /* Expander, shared rails and reset consumers remain board-owned.
     * Only the RGB driver/framebuffer is owned by this failed initializer. */
    if (s_panel) {
        if (esp_lcd_panel_del(s_panel) == ESP_OK) s_panel = NULL;
        else ESP_LOGE(TAG, "RGB rollback incomplete; reset required");
    }
    return ret;
}

esp_err_t bsp_display_backlight_set(bool on)
{
    ESP_RETURN_ON_FALSE(s_ready, ESP_ERR_INVALID_STATE, TAG,
                        "display not initialized");
    ESP_RETURN_ON_ERROR(gpio_set_level(BSP_PIN_LCD_BACKLIGHT, on ? 1 : 0),
                        TAG, "backlight");
    s_backlight = on;
    return ESP_OK;
}

bool bsp_display_backlight_get(void)
{
    return s_backlight;
}
