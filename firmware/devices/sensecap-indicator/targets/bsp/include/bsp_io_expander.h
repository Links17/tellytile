/* SenseCAP Indicator TCA9535 IO expander.
 * Drives the ST7701S chip-select/reset, touch reset, RP2040 reset and the
 * BMP sensor power rail. */
#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Serialized task-context startup only; do not race init/get_bus against
 * runtime operations or call any API from an ISR/non-blocking callback.
 * Initializes the board I2C bus and probes the expander (0x20, then 0x39).
 * Ready state requires all four shadow registers. Repeated success is a
 * no-op. Failed init releases owned device/bus/mutex; complete cleanup permits
 * retry, incomplete cleanup requires reset and blocks reuse. After success
 * the BSP owns them for the process lifetime; borrowers must not delete them.
 *
 * After startup each pin operation locks the expander for the complete read,
 * shadow recovery or read-modify-write. Different tasks may operate different
 * owned pins without losing another pin's update. A write error invalidates
 * that port's shadow; the next update rereads it before changing any bit.
 * Only the latest successful value is cached. Physical state after a failed
 * write is uncertain. Multi-call reset/bit-bang sequences need one pin owner
 * or an application lock; the internal lock does not serialize a sequence.
 *
 * Runtime mutex wait is 100 ms converted to ticks (minimum one tick). Each
 * I2C transaction uses 100 ms; a shadow recovery needs read plus write.
 * Scheduler/SDK overhead is additional; no hard whole-call deadline is
 * promised. Lock timeout performs no I/O. Init performs two probes at most
 * and four register reads, plus native allocation/cleanup without a deadline. */
esp_err_t bsp_io_expander_init(void);

/* pin: 0-15. direction: 1 = output, 0 = input. */
esp_err_t bsp_io_expander_set_direction(int pin, int output);

/* pin: 0-15, must be configured as output. */
esp_err_t bsp_io_expander_set_level(int pin, int level);

/* pin: 0-15. Reads the input register regardless of direction. level must
 * be non-NULL; caller output is preserved on any error. */
esp_err_t bsp_io_expander_get_level(int pin, int *level);

/* Returns the shared board I2C bus (SDA=39, SCL=40, 100 kHz) created by
 * bsp_io_expander_init(). Other on-board I2C devices (touch, sensors)
 * attach to this bus. Calls bsp_io_expander_init() if needed during serialized
 * startup. out_bus is required and cleared on failure. Bus API serialization
 * does not protect multi-transaction protocols of its attached devices. */
esp_err_t bsp_i2c_get_bus(i2c_master_bus_handle_t *out_bus);

#ifdef __cplusplus
}
#endif
