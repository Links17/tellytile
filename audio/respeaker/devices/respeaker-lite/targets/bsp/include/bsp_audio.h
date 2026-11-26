/* reSpeaker Lite audio: XMOS mic processor + AIC3204 codec over I2C,
 * 16 kHz I2S slave. Derived from the imported hardware reference (Apache 2.0),
 * see NOTICE. */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Task context only, never ISR/non-blocking button/UI callbacks. Serialize
 * startup before publishing readiness to workers. Thereafter one RX-owning
 * task (or application mutex) serializes mic start/read/stop; one TX owner
 * serializes writes. RX and TX may run concurrently. Control operations use
 * a separate internal mutex spanning each complete XMOS request/response or
 * codec page/gain/mute sequence. They may run alongside RX/TX. Preserve the
 * application's LVGL lock for any separate LVGL operations.
 *
 * Initialize I2C (XMOS + codec), pulse the XMOS reset line, verify the XMOS
 * answers with its firmware version, then start the I2S slave channels.
 * Returns ESP_ERR_* if the XMOS does not answer — check that the XMOS runs
 * Seeed's 16 kHz I2S firmware v1.0.9 (ESP32 flashing does not update it).
 * A readable version alone does not identify/validate the I2S variant.
 * Ready requires TX enable and initial codec gain to succeed. Successful
 * repeated init is a no-op. Failed init rolls back owned resources; complete
 * cleanup allows retry, incomplete cleanup requires reset. XMOS/reset/codec
 * physical state is uncertain after errors, even after successful cleanup.
 * All handles are BSP-owned for the process lifetime after success. There
 * is no runtime bus/channel deletion or shutdown/reconfiguration contract.
 * Init includes vendor reset delays (20 + 3000 ms) and native setup/cleanup
 * waits. No hard whole-call deadline or physical recovery guarantee exists. */
esp_err_t bsp_audio_init(void);

/* Ready-only codec gain, -63.5 dB..0 dB; percent clamps to 0..100. Mutex
 * timeout performs no I/O. Later I2C failure can leave a partially applied
 * gain/mute sequence; retry the complete requested setting. Control mutex
 * wait: 100 ms converted to ticks (minimum one); each I2C transaction: 100 ms.
 * Volume uses up to four transactions; mute read uses two. Scheduling and
 * SDK overhead are additional, so these are not hard total deadlines. */
esp_err_t bsp_audio_set_volume(int percent);

/* Hardware mute state reported by the XMOS (firmware >= 1.0.9).
 * Returns true (fail closed) when the state cannot be read. */
bool bsp_audio_muted(void);

/* Capture: interleaved stereo 32-bit frames at 16 kHz. Start is idempotent
 * when ready/running. Stop is idempotent when already stopped. A failed stop
 * logs the error and blocks read/start while retaining channel ownership;
 * retry stop until it succeeds (a subsequent start can then succeed).
 * Native enable/disable may wait indefinitely; no hard stop deadline.
 *
 * Read/write borrow buffers only during the call. Require non-NULL buffers,
 * frames > 0 and frames <= INT_MAX / (2 * sizeof(int32_t)). Returns complete
 * frames transferred, including partial progress on native timeout/error.
 * Zero also covers invalid input/not-ready/no progress; this size_t API does
 * not distinguish those errors. The unread output tail is unspecified.
 * timeout_ms is passed as milliseconds to native I2S, where each channel/DMA
 * wait can consume that budget. It is not a deadline for the whole request.
 * RX reads must not race stop/start; callers must not mutate borrowed buffers. */
esp_err_t bsp_audio_mic_start(void);
void      bsp_audio_mic_stop(void);
size_t    bsp_audio_mic_read(int32_t *stereo_out, size_t frames, uint32_t timeout_ms);

/* Speaker playback, using the buffer/timeout contract above. Returned frames
 * were copied into native DMA buffers, not necessarily played by the speaker. */
size_t    bsp_audio_write(const int32_t *stereo_in, size_t frames, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
