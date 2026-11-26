/* TCA9535 16-bit IO expander on the board I2C bus.
 * Register map per TI TCA9535 datasheet (SLASE01): 0/1 input, 2/3 output,
 * 4/5 polarity inversion, 6/7 configuration (1 = input).
 * Address facts: Seeed-Solution/SenseCAP_Indicator_ESP32 (Apache 2.0)
 * sensecap_indicator_board.c — probe 0x20, fall back to 0x39. */
#include "bsp_io_expander.h"
#include "bsp_pins.h"

#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <stdbool.h>

static const char *TAG = "bsp_io_expander";

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_dev;
static SemaphoreHandle_t s_lock;

/* Shadow copies of the output (reg 2/3) and config (reg 6/7) registers so
 * single-pin updates never clobber the other pins on the same port. */
static uint8_t s_output[2];
static uint8_t s_config[2];
static bool s_output_valid[2];
static bool s_config_valid[2];
static bool s_shadow_valid;

static esp_err_t exp_write_reg(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(s_dev, buf, sizeof(buf), 100);
}

static esp_err_t exp_read_reg(uint8_t reg, uint8_t *val)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, val, 1, 100);
}

esp_err_t bsp_io_expander_init(void)
{
    if (s_shadow_valid) {
        return ESP_OK;
    }
    ESP_RETURN_ON_FALSE(!s_bus && !s_dev && !s_lock, ESP_ERR_INVALID_STATE, TAG,
                        "incomplete initialization rollback; reset required");
    esp_err_t ret;
    uint8_t output[2], config[2];
    s_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_lock, ESP_ERR_NO_MEM, TAG, "expander mutex");
    {
        i2c_master_bus_config_t bus_cfg = {
            .i2c_port = -1,
            .sda_io_num = BSP_PIN_I2C_SDA,
            .scl_io_num = BSP_PIN_I2C_SCL,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .flags.enable_internal_pullup = true,
        };
        ESP_GOTO_ON_ERROR(i2c_new_master_bus(&bus_cfg, &s_bus), fail, TAG, "bus");
    }
    /* Probe before attaching a device so a failed primary address never
     * leaves an owned handle to overwrite when selecting the fallback. */
    uint8_t addr = BSP_I2C_ADDR_EXPANDER_PRIMARY;
    ret = i2c_master_probe(s_bus, addr, 100);
    if (ret != ESP_OK) {
        addr = BSP_I2C_ADDR_EXPANDER_FALLBACK;
        ESP_GOTO_ON_ERROR(i2c_master_probe(s_bus, addr, 100), fail, TAG,
                          "TCA9535 not found");
    }
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = BSP_I2C_FREQ_HZ,
    };
    ESP_GOTO_ON_ERROR(i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev), fail, TAG,
                      "add device 0x%02x", addr);

    /* Sync the shadows with the real chip state. */
    ESP_GOTO_ON_ERROR(exp_read_reg(2, &output[0]), fail, TAG, "read output0");
    ESP_GOTO_ON_ERROR(exp_read_reg(3, &output[1]), fail, TAG, "read output1");
    ESP_GOTO_ON_ERROR(exp_read_reg(6, &config[0]), fail, TAG, "read config0");
    ESP_GOTO_ON_ERROR(exp_read_reg(7, &config[1]), fail, TAG, "read config1");
    for (int port = 0; port < 2; ++port) {
        s_output[port] = output[port];
        s_config[port] = config[port];
        s_output_valid[port] = true;
        s_config_valid[port] = true;
    }
    s_shadow_valid = true;
    return ESP_OK;

fail:
    /* The bus cannot have been borrowed before this first, serialized init
     * succeeds. Afterwards it has board-wide, process-lifetime ownership.
     * Never free a bus while its device removal has failed. */
    if (s_dev) {
        if (i2c_master_bus_rm_device(s_dev) != ESP_OK) {
            ESP_LOGE(TAG, "expander rollback incomplete; reset required");
            return ret;
        }
        s_dev = NULL;
    }
    if (s_bus) {
        if (i2c_del_master_bus(s_bus) != ESP_OK) {
            ESP_LOGE(TAG, "bus rollback incomplete; reset required");
            return ret;
        }
        s_bus = NULL;
    }
    vSemaphoreDelete(s_lock);
    s_lock = NULL;
    return ret;
}

esp_err_t bsp_i2c_get_bus(i2c_master_bus_handle_t *out_bus)
{
    ESP_RETURN_ON_FALSE(out_bus != NULL, ESP_ERR_INVALID_ARG, TAG, "out_bus");
    *out_bus = NULL;
    ESP_RETURN_ON_ERROR(bsp_io_expander_init(), TAG, "init");
    *out_bus = s_bus;
    return ESP_OK;
}

static esp_err_t exp_update_bit(uint8_t reg, uint8_t *shadow, bool *valid,
                                uint8_t mask, bool set)
{
    /* A timed-out write may have reached the chip. Resynchronize that port
     * before any later read-modify-write; never assume the previous value. */
    if (!*valid) {
        ESP_RETURN_ON_ERROR(exp_read_reg(reg, shadow), TAG, "resync shadow");
        *valid = true;
    }
    uint8_t next = set ? (*shadow | mask) : (*shadow & ~mask);
    esp_err_t err = exp_write_reg(reg, next);
    *valid = err == ESP_OK;
    if (*valid) *shadow = next;
    return err;
}

static esp_err_t exp_lock(void)
{
    TickType_t wait = pdMS_TO_TICKS(100);
    return xSemaphoreTake(s_lock, wait ? wait : 1) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t bsp_io_expander_set_direction(int pin, int output)
{
    ESP_RETURN_ON_FALSE(s_dev && s_shadow_valid, ESP_ERR_INVALID_STATE, TAG,
                        "not initialized");
    ESP_RETURN_ON_FALSE(pin >= 0 && pin < 16, ESP_ERR_INVALID_ARG, TAG, "pin");
    uint8_t mask = 1u << (pin & 7);
    int port = pin >> 3;
    ESP_RETURN_ON_ERROR(exp_lock(), TAG, "expander mutex");
    esp_err_t ret = exp_update_bit(6 + port, &s_config[port], &s_config_valid[port], mask, !output);
    xSemaphoreGive(s_lock);
    return ret;
}

esp_err_t bsp_io_expander_set_level(int pin, int level)
{
    ESP_RETURN_ON_FALSE(s_dev && s_shadow_valid, ESP_ERR_INVALID_STATE, TAG,
                        "not initialized");
    ESP_RETURN_ON_FALSE(pin >= 0 && pin < 16, ESP_ERR_INVALID_ARG, TAG, "pin");
    uint8_t mask = 1u << (pin & 7);
    int port = pin >> 3;
    ESP_RETURN_ON_ERROR(exp_lock(), TAG, "expander mutex");
    esp_err_t ret = exp_update_bit(2 + port, &s_output[port], &s_output_valid[port], mask, level != 0);
    xSemaphoreGive(s_lock);
    return ret;
}

esp_err_t bsp_io_expander_get_level(int pin, int *level)
{
    ESP_RETURN_ON_FALSE(s_shadow_valid, ESP_ERR_INVALID_STATE, TAG, "not initialized");
    ESP_RETURN_ON_FALSE(pin >= 0 && pin < 16 && level, ESP_ERR_INVALID_ARG,
                        TAG, "pin");
    uint8_t val;
    ESP_RETURN_ON_ERROR(exp_lock(), TAG, "expander mutex");
    esp_err_t ret = exp_read_reg(pin >> 3, &val);
    if (ret == ESP_OK) *level = (val >> (pin & 7)) & 1;
    xSemaphoreGive(s_lock);
    return ret;
}
