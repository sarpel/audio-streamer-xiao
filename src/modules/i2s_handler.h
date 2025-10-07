#ifndef I2S_HANDLER_H
#define I2S_HANDLER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/**
 * Initialize I2S driver for INMP441 MEMS microphone
 *
 * Configures:
 * - 48 kHz sample rate
 * - 24-bit samples (stored in 32-bit containers)
 * - Mono (left channel only)
 * - I2S standard mode
 * - DMA buffers for continuous streaming
 *
 * @return true on success, false on failure
 */
bool i2s_handler_init(void);

/**
 * Read audio samples from I2S DMA buffer
 *
 * Reads 24-bit samples stored in 32-bit int containers.
 * Blocks until requested number of samples are available.
 *
 * @param buffer Output buffer for int32_t samples
 * @param samples_to_read Number of samples to read
 * @param bytes_read Pointer to store actual bytes read
 * @return true on success, false on failure
 */
bool i2s_handler_read(int32_t* buffer, size_t samples_to_read, size_t* bytes_read);

/**
 * Deinitialize I2S driver
 */
void i2s_handler_deinit(void);

/**
 * Get I2S driver statistics
 *
 * @param overflow_count Pointer to store DMA overflow count
 * @param underflow_count Pointer to store DMA underflow count
 */
void i2s_handler_get_stats(uint32_t* overflow_count, uint32_t* underflow_count);

#endif // I2S_HANDLER_H
