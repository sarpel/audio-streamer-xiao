#ifndef BUFFER_MANAGER_H
#define BUFFER_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/**
 * Initialize ring buffer for audio samples
 *
 * Allocates buffer in PSRAM for large capacity (512 KB).
 * Uses mutex for thread-safe access between I2S reader and TCP sender.
 *
 * @param size Buffer size in bytes
 * @return true on success, false on failure
 */
bool buffer_manager_init(size_t size);

/**
 * Write samples to ring buffer
 *
 * Thread-safe write operation. Blocks if buffer is full (backpressure).
 *
 * @param data Pointer to int32_t samples
 * @param samples Number of samples to write
 * @return Number of samples actually written
 */
size_t buffer_manager_write(const int32_t* data, size_t samples);

/**
 * Read samples from ring buffer
 *
 * Thread-safe read operation. Blocks if buffer is empty.
 *
 * @param data Output buffer for int32_t samples
 * @param samples Number of samples to read
 * @return Number of samples actually read
 */
size_t buffer_manager_read(int32_t* data, size_t samples);

/**
 * Get available samples for reading
 *
 * @return Number of samples available
 */
size_t buffer_manager_available(void);

/**
 * Get free space for writing
 *
 * @return Number of samples that can be written
 */
size_t buffer_manager_free_space(void);

/**
 * Get buffer usage percentage
 *
 * @return Usage percentage (0-100)
 */
uint8_t buffer_manager_usage_percent(void);

/**
 * Check if buffer overflow occurred
 *
 * @return true if overflow detected since last check
 */
bool buffer_manager_check_overflow(void);

/**
 * Reset buffer (clear all data)
 */
void buffer_manager_reset(void);

/**
 * Deinitialize buffer manager
 */
void buffer_manager_deinit(void);

#endif // BUFFER_MANAGER_H
