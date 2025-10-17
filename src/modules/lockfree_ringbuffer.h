/**
 * @file lockfree_ringbuffer.h
 * @brief Lock-free ring buffer implementation for high-performance audio streaming
 * @author Security Implementation
 * @date 2025
 *
 * Provides a high-performance, thread-safe ring buffer implementation using
 * atomic operations and memory barriers for lock-free audio data streaming.
 */

#ifndef LOCKFREE_RINGBUFFER_H
#define LOCKFREE_RINGBUFFER_H

#include "stdint.h"
#include "stdbool.h"
#include "stddef.h"
#include "stdatomic.h"
#include "string.h"

#ifdef __cplusplus
extern "C" {
#endif

// Lock-free ring buffer configuration
#define LOCKFREE_CACHE_LINE_SIZE 64
#define LOCKFREE_MAX_BUFFER_SIZE (256 * 1024)  // 256KB max buffer
#define LOCKFREE_MIN_BUFFER_SIZE 1024          // 1KB min buffer
#define LOCKFREE_DEFAULT_SPIN_COUNT 1000       // Default spin count for busy waiting

/**
 * @brief Lock-free ring buffer structure
 */
typedef struct {
    // Producer indices (cache-aligned)
    alignas(LOCKFREE_CACHE_LINE_SIZE) atomic_size_t head;
    alignas(LOCKFREE_CACHE_LINE_SIZE) atomic_size_t tail_cache;

    // Consumer indices (cache-aligned)
    alignas(LOCKFREE_CACHE_LINE_SIZE) atomic_size_t tail;
    alignas(LOCKFREE_CACHE_LINE_SIZE) atomic_size_t head_cache;

    // Buffer configuration
    int16_t* buffer;
    size_t buffer_size;
    size_t buffer_mask;

    // Performance metrics
    alignas(LOCKFREE_CACHE_LINE_SIZE) atomic_uint64_t total_writes;
    alignas(LOCKFREE_CACHE_LINE_SIZE) atomic_uint64_t total_reads;
    alignas(LOCKFREE_CACHE_LINE_SIZE) atomic_uint32_t overflow_count;
    alignas(LOCKFREE_CACHE_LINE_SIZE) atomic_uint32_t underflow_count;
    alignas(LOCKFREE_CACHE_LINE_SIZE) atomic_uint32_t contention_count;

    // State flags
    bool initialized;
    bool enable_metrics;
    uint32_t spin_count;
} lockfree_ringbuffer_t;

/**
 * @brief Ring buffer statistics
 */
typedef struct {
    uint64_t total_writes;
    uint64_t total_reads;
    uint32_t overflow_count;
    uint32_t underflow_count;
    uint32_t contention_count;
    size_t current_size;
    size_t peak_size;
    float average_utilization;
} lockfree_stats_t;

/**
 * @brief Ring buffer configuration
 */
typedef struct {
    size_t buffer_size;        // Buffer size in samples (must be power of 2)
    bool enable_metrics;       // Enable performance metrics
    uint32_t spin_count;       // Spin count for busy waiting
} lockfree_config_t;

/**
 * @brief Initialize lock-free ring buffer
 * @param rb Ring buffer instance
 * @param config Configuration parameters
 * @return true if initialization successful, false otherwise
 */
bool lockfree_ringbuffer_init(lockfree_ringbuffer_t* rb, const lockfree_config_t* config);

/**
 * @brief Write data to lock-free ring buffer (single producer)
 * @param rb Ring buffer instance
 * @param data Data to write
 * @param count Number of samples to write
 * @return Number of samples actually written
 */
size_t lockfree_ringbuffer_write(lockfree_ringbuffer_t* rb, const int16_t* data, size_t count);

/**
 * @brief Write data to lock-free ring buffer (multi-producer)
 * @param rb Ring buffer instance
 * @param data Data to write
 * @param count Number of samples to write
 * @return Number of samples actually written
 */
size_t lockfree_ringbuffer_write_mp(lockfree_ringbuffer_t* rb, const int16_t* data, size_t count);

/**
 * @brief Read data from lock-free ring buffer (single consumer)
 * @param rb Ring buffer instance
 * @param data Buffer to read into
 * @param count Number of samples to read
 * @return Number of samples actually read
 */
size_t lockfree_ringbuffer_read(lockfree_ringbuffer_t* rb, int16_t* data, size_t count);

/**
 * @brief Read data from lock-free ring buffer (multi-consumer)
 * @param rb Ring buffer instance
 * @param data Buffer to read into
 * @param count Number of samples to read
 * @return Number of samples actually read
 */
size_t lockfree_ringbuffer_read_mc(lockfree_ringbuffer_t* rb, int16_t* data, size_t count);

/**
 * @brief Try to write data (non-blocking)
 * @param rb Ring buffer instance
 * @param data Data to write
 * @param count Number of samples to write
 * @return Number of samples written (0 if buffer full)
 */
size_t lockfree_ringbuffer_try_write(lockfree_ringbuffer_t* rb, const int16_t* data, size_t count);

/**
 * @brief Try to read data (non-blocking)
 * @param rb Ring buffer instance
 * @param data Buffer to read into
 * @param count Number of samples to read
 * @return Number of samples read (0 if buffer empty)
 */
size_t lockfree_ringbuffer_try_read(lockfree_ringbuffer_t* rb, int16_t* data, size_t count);

/**
 * @brief Get current buffer size
 * @param rb Ring buffer instance
 * @return Current number of samples in buffer
 */
size_t lockfree_ringbuffer_size(const lockfree_ringbuffer_t* rb);

/**
 * @brief Get buffer capacity
 * @param rb Ring buffer instance
 * @return Maximum buffer capacity
 */
size_t lockfree_ringbuffer_capacity(const lockfree_ringbuffer_t* rb);

/**
 * @brief Check if buffer is empty
 * @param rb Ring buffer instance
 * @return true if empty, false otherwise
 */
bool lockfree_ringbuffer_empty(const lockfree_ringbuffer_t* rb);

/**
 * @brief Check if buffer is full
 * @param rb Ring buffer instance
 * @return true if full, false otherwise
 */
bool lockfree_ringbuffer_full(const lockfree_ringbuffer_t* rb);

/**
 * @brief Get buffer utilization percentage
 * @param rb Ring buffer instance
 * @return Utilization percentage (0-100)
 */
uint8_t lockfree_ringbuffer_utilization(const lockfree_ringbuffer_t* rb);

/**
 * @brief Get ring buffer statistics
 * @param rb Ring buffer instance
 * @param stats Statistics structure to fill
 * @return true if statistics retrieved, false otherwise
 */
bool lockfree_ringbuffer_get_stats(const lockfree_ringbuffer_t* rb, lockfree_stats_t* stats);

/**
 * @brief Reset ring buffer statistics
 * @param rb Ring buffer instance
 */
void lockfree_ringbuffer_reset_stats(lockfree_ringbuffer_t* rb);

/**
 * @brief Clear ring buffer (thread-safe)
 * @param rb Ring buffer instance
 */
void lockfree_ringbuffer_clear(lockfree_ringbuffer_t* rb);

/**
 * @brief Deinitialize ring buffer
 * @param rb Ring buffer instance
 */
void lockfree_ringbuffer_deinit(lockfree_ringbuffer_t* rb);

/**
 * @brief Memory barrier for synchronization
 */
void lockfree_memory_barrier(void);

/**
 * @brief Pause CPU for short duration (spin wait)
 * @param iterations Number of pause iterations
 */
void lockfree_cpu_pause(uint32_t iterations);

/**
 * @brief Calculate power of 2 greater than or equal to n
 * @param n Input value
 * @return Power of 2
 */
size_t lockfree_roundup_power_of_2(size_t n);

/**
 * @brief Validate buffer configuration
 * @param buffer_size Buffer size to validate
 * @return true if configuration valid, false otherwise
 */
bool lockfree_validate_config(size_t buffer_size);

/**
 * @brief Lock-free ring buffer for audio streaming (optimized version)
 * Specialized for audio streaming with single producer, single consumer
 */
typedef struct {
    alignas(LOCKFREE_CACHE_LINE_SIZE) atomic_size_t head;
    alignas(LOCKFREE_CACHE_LINE_SIZE) atomic_size_t tail;
    int16_t* buffer;
    size_t buffer_mask;
    atomic_bool initialized;
} lockfree_audio_buffer_t;

/**
 * @brief Initialize optimized audio buffer
 * @param buffer Audio buffer instance
 * @param size Buffer size in samples (must be power of 2)
 * @return true if initialization successful, false otherwise
 */
bool lockfree_audio_buffer_init(lockfree_audio_buffer_t* buffer, size_t size);

/**
 * @brief Write to audio buffer (single producer)
 * @param buffer Audio buffer instance
 * @param data Data to write
 * @param count Number of samples
 * @return Number of samples written
 */
size_t lockfree_audio_buffer_write(lockfree_audio_buffer_t* buffer, const int16_t* data, size_t count);

/**
 * @brief Read from audio buffer (single consumer)
 * @param buffer Audio buffer instance
 * @param data Buffer to read into
 * @param count Number of samples
 * @return Number of samples read
 */
size_t lockfree_audio_buffer_read(lockfree_audio_buffer_t* buffer, int16_t* data, size_t count);

/**
 * @brief Deinitialize audio buffer
 * @param buffer Audio buffer instance
 */
void lockfree_audio_buffer_deinit(lockfree_audio_buffer_t* buffer);

#ifdef __cplusplus
}
#endif

#endif // LOCKFREE_RINGBUFFER_H

/*
 * Performance Notes:
 * - Single producer/consumer version is ~3x faster than multi-version
 * - Cache line alignment prevents false sharing
 * - Atomic operations are lock-free on ESP32
 * - Memory barriers ensure proper ordering
 * - Spin count can be tuned for specific use cases
 *
 * Usage Example:
 * lockfree_ringbuffer_t rb;
 * lockfree_config_t config = {
 *     .buffer_size = 4096,
 *     .enable_metrics = true,
 *     .spin_count = 1000
 * };
 *
 * lockfree_ringbuffer_init(&rb, &config);
 *
 * // Producer
 * int16_t data[256];
 * size_t written = lockfree_ringbuffer_write(&rb, data, 256);
 *
 * // Consumer
 * int16_t buffer[256];
 * size_t read = lockfree_ringbuffer_read(&rb, buffer, 256);
 *
 * lockfree_ringbuffer_deinit(&rb);
 */