/* reSpeaker Lite button: XIAO BOOT on GPIO 0, active low.
 * Callbacks run in the BSP button task and must stay non-blocking. */
#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*bsp_button_cb_t)(bool pressed, void *arg);

/* Polls the button with debounce in a dedicated task. */
/* Task-context serialized startup only; no ISR/callback initialization.
 * One process-lifetime polling task owns sampling/debounce. Repeating the
 * same callback is a no-op; replacement returns INVALID_STATE.
 * GPIO/task creation failure permits retry and does not publish a callback
 * registration. GPIO configuration may remain applied after failed startup.
 * NULL callback selects polling-only mode. Callback code must remain
 * valid until reset; no unregister/shutdown is provided. A callback may run
 * as soon as its task starts, including before init returns. Callbacks must
 * not block: queue work to another task; follow LVGL locking separately.
 * Native GPIO/task creation has no BSP-imposed total deadline.
 *
 * pressed() atomically returns the latest debounced sample, false before a
 * sample/initialization. Concurrent task reads are supported.
 * It does not wait for fresh hardware input. The worker polls nominally every
 * 20 ms (at least one tick) with 30 ms debounce; tick rounding, callback work
 * and scheduling affect latency. No maximum input/callback latency is promised.
 * The BSP reserves its button GPIOs for the process lifetime. */
esp_err_t bsp_button_init(bsp_button_cb_t cb);

/* Level without a callback, true while held. */
bool bsp_button_pressed(void);

#ifdef __cplusplus
}
#endif
