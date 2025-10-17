/**
 * @file buffer_manager_secure.h
 * @brief Secure buffer manager with priority inheritance and overflow protection
 * @author Security Implementation
 * @date 2025
 *
 * Enhanced buffer manager with security fixes, priority inheritance,
 * and comprehensive bounds checking to prevent buffer overflow vulnerabilities.
 */

#ifndef BUFFER_MANAGER_SECURE_H
#define BUFFER_MANAGER_SECURE_H

#include "buffer_manager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif

// Enhanced security parameters
#define BUFFER_MAX_SIZE_SAMPLES (256 * 1024)  // 256KB max buffer size
#define BUFFER_MIN_SIZE_SAMPLES (1024)        // 1KB min buffer size
#define BUFFER_OVERFLOW_THRESHOLD 95          // 95% usage triggers overflow protection
#define BUFFER_UNDERFLOW_THRESHOLD 5          // 5% usage triggers underflow warning

// Timeout configurations for security
#define BUFFER_OPERATION_TIMEOUT_MS 5000      // 5 second timeout for buffer operations
#define BUFFER_RESIZE_TIMEOUT_MS 10000        // 10 second timeout for resize operations

/**
 * @brief Priority inheritance mutex for preventing priority inversion
 */
typedef struct {
    SemaphoreHandle_t mutex;
    TaskHandle_t owner;
    UBaseType_t original_priority;
    UBaseType_t inherited_priority;
    bool has_inherited;
} PriorityInheritanceMutex_t;

/**
 * @brief Enhanced buffer statistics with security metrics
 */
typedef struct {
    size_t size_samples;
    size_t used_samples;
    size_t write_index;
    size_t read_index;
    uint32_t overflow_count;
    uint32_t underflow_count;
    uint32_t resize_count;
    uint32_t timeout_count;
    uint32_t max_concurrent_access;
    uint64_t total_bytes_written;
    uint64_t total_bytes_read;
    float average_utilization;
    uint32_t peak_utilization_percent;
    TickType_t last_overflow_tick;
    TickType_t last_underflow_tick;
} secure_buffer_stats_t;

/**
 * @brief Buffer validation result
 */
typedef enum {
    BUFFER_VALIDATION_OK,
    BUFFER_VALIDATION_INVALID_SIZE,
    BUFFER_VALIDATION_SIZE_TOO_LARGE,
    BUFFER_VALIDATION_SIZE_TOO_SMALL,
    BUFFER_VALIDATION_NULL_POINTER,
    BUFFER_VALIDATION_OVERFLOW_RISK,
    BUFFER_VALIDATION_UNDERFLOW_RISK
} buffer_validation_result_t;

/**
 * @brief Initialize secure buffer manager
 * @param size_samples Initial buffer size in samples
 * @return true if initialization successful, false otherwise
 */
bool buffer_manager_secure_init(size_t size_samples);

/**
 * @brief Write data to secure buffer with bounds checking
 * @param data Pointer to data to write
 * @param samples Number of samples to write
 * @return Number of samples actually written, 0 on error
 */
size_t buffer_manager_secure_write(const int16_t* data, size_t samples);

/**
 * @brief Read data from secure buffer with bounds checking
 * @param data Pointer to buffer for reading data
 * @param samples Number of samples to read
 * @return Number of samples actually read, 0 on error
 */
size_t buffer_manager_secure_read(int16_t* data, size_t samples);

/**
 * @brief Resize buffer with comprehensive validation
 * @param new_size_samples New buffer size in samples
 * @return true if resize successful, false otherwise
 */
bool buffer_manager_secure_resize(size_t new_size_samples);

/**
 * @brief Get secure buffer statistics
 * @param stats Pointer to statistics structure
 * @return true if statistics retrieved, false otherwise
 */
bool buffer_manager_secure_get_stats(secure_buffer_stats_t* stats);

/**
 * @brief Validate buffer parameters
 * @param size_samples Buffer size to validate
 * @param data Pointer to validate (optional)
 * @param samples Number of samples to validate
 * @return Validation result
 */
buffer_validation_result_t buffer_manager_secure_validate(size_t size_samples, const int16_t* data, size_t samples);

/**
 * @brief Check for buffer overflow risk
 * @param requested_samples Number of samples to be written
 * @return true if overflow risk detected, false otherwise
 */
bool buffer_manager_secure_overflow_risk(size_t requested_samples);

/**
 * @brief Get current buffer utilization percentage
 * @return Utilization percentage (0-100)
 */
uint8_t buffer_manager_secure_get_utilization_percent(void);

/**
 * @brief Force buffer reset with security clearing
 * @return true if reset successful, false otherwise
 */
bool buffer_manager_secure_reset(void);

/**
 * @brief Deinitialize secure buffer manager
 */
void buffer_manager_secure_deinit(void);

/**
 * @brief Priority inheritance mutex operations
 */
bool priority_inheritance_mutex_create(PriorityInheritanceMutex_t* mutex);
bool priority_inheritance_mutex_take(PriorityInheritanceMutex_t* mutex, TickType_t timeout);
bool priority_inheritance_mutex_give(PriorityInheritanceMutex_t* mutex);
void priority_inheritance_mutex_destroy(PriorityInheritanceMutex_t* mutex);

/**
 * @brief Secure memory operations with clearing
 */
void secure_memzero(void* ptr, size_t size);
bool secure_memcpy(void* dest, size_t dest_size, const void* src, size_t src_size);

#ifdef __cplusplus
}
#endif

#endif // BUFFER_MANAGER_SECURE_H