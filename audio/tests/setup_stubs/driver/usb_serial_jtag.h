#pragma once
#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>
typedef struct { int tx_buffer_size, rx_buffer_size; } usb_serial_jtag_driver_config_t;
esp_err_t usb_serial_jtag_driver_install(usb_serial_jtag_driver_config_t *);
void usb_serial_jtag_driver_uninstall(void);
int usb_serial_jtag_read_bytes(void *, uint32_t, uint32_t);
int usb_serial_jtag_write_bytes(const void *, size_t, uint32_t);
esp_err_t usb_serial_jtag_wait_tx_done(uint32_t);
