/**
 * @file buffer_manager_secure.cpp
 * @brief Secure buffer manager implementation with priority inheritance
 * @author Security Implementation
 * @date 2025
 *
 * Enhanced buffer manager with security fixes, priority inheritance,
 * and comprehensive bounds checking to prevent buffer overflow vulnerabilities.
 */

#include "buffer_manager_secure.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "string.h"
#include "math.h"

static const char* TAG = "BUFFER_MANAGER_SECURE";

// Global buffer state with enhanced security
static struct {
    int16_t* ring_buffer;
    size_t buffer_size_samples;
    size_t buffer_size_bytes;
    volatile size_t write_index;
    volatile size_t read_index;
    volatile size_t used_samples;

    // Enhanced statistics
    secure_buffer_stats_t stats;

    // Security features
    PriorityInheritanceMutex_t access_mutex;
    SemaphoreHandle_t stats_mutex;

    // Overflow protection
    volatile bool overflow_detected;
    volatile bool underflow_detected;
    volatile size_t overflow_threshold;
    volatile size_t underflow_threshold;

    // State tracking
    bool initialized;
    uint32_t initialization_magic;
} secure_buffer = {0};

#define BUFFER_MAGIC 0x42554646  // "BUFF" in hex
#define MIN_FREE_SPACE_THRESHOLD 64  // Minimum free space in samples

/**
 * @brief Priority inheritance mutex implementation
 */
bool priority_inheritance_mutex_create(PriorityInheritanceMutex_t* mutex) {
    if (mutex == NULL) {
        return false;
    }

    mutex->mutex = xSemaphoreCreateMutex();
    if (mutex->mutex == NULL) {
        return false;
    }

    mutex->owner = NULL;
    mutex->original_priority = 0;
    mutex->inherited_priority = 0;
    mutex->has_inherited = false;

    return true;
}

bool priority_inheritance_mutex_take(PriorityInheritanceMutex_t* mutex, TickType_t timeout) {
    if (mutex == NULL || mutex->mutex == NULL) {
        return false;
    }

    TaskHandle_t current_task = xTaskGetCurrentTaskHandle();
    UBaseType_t current_priority = uxTaskPriorityGet(current_task);

    // Check if we already own the mutex
    if (mutex->owner == current_task) {
        return true;  // Recursive lock - already owned
    }

    // If another task owns the mutex, check for priority inheritance
    if (mutex->owner != NULL && mutex->has_inherited) {
        UBaseType_t owner_priority = uxTaskPriorityGet(mutex->owner);

        // If current task has higher priority, inherit it to the owner
        if (current_priority > owner_priority) {
            ESP_LOGD(TAG, "Priority inheritance: task %p (prio %d) inheriting to owner %p (prio %d)",
                     current_task, current_priority, mutex->owner, owner_priority);

            // Store owner's original priority if not already stored
            if (!mutex->has_inherited) {
                mutex->original_priority = owner_priority;
            }

            // Inherit higher priority to owner task
            vTaskPrioritySet(mutex->owner, current_priority);
            mutex->inherited_priority = current_priority;
            mutex->has_inherited = true;
        }
    }

    // Take the mutex
    if (xSemaphoreTake(mutex->mutex, timeout) != pdTRUE) {
        return false;
    }

    // Now we own the mutex
    mutex->owner = current_task;

    return true;
}

bool priority_inheritance_mutex_give(PriorityInheritanceMutex_t* mutex) {
    if (mutex == NULL || mutex->mutex == NULL) {
        return false;
    }

    TaskHandle_t current_task = xTaskGetCurrentTaskHandle();

    // Verify current task owns the mutex
    if (mutex->owner != current_task) {
        ESP_LOGE(TAG, "Task %p trying to give mutex it doesn't own", current_task);
        return false;
    }

    // Restore original priority if inheritance was used
    if (mutex->has_inherited) {
        ESP_LOGD(TAG, "Restoring original priority %d for task %p",
                 mutex->original_priority, current_task);
        vTaskPrioritySet(current_task, mutex->original_priority);
        mutex->has_inherited = false;
        mutex->inherited_priority = 0;
    }

    // Clear ownership
    mutex->owner = NULL;

    // Release the mutex
    return xSemaphoreGive(mutex->mutex) == pdTRUE;
}

void priority_inheritance_mutex_destroy(PriorityInheritanceMutex_t* mutex) {
    if (mutex == NULL) {
        return;
    }

    if (mutex->mutex != NULL) {
        vSemaphoreDelete(mutex->mutex);
        mutex->mutex = NULL;
    }

    mutex->owner = NULL;
    mutex->has_inherited = false;
}

/**
 * @brief Secure memory operations
 */
void secure_memzero(void* ptr, size_t size) {
    if (ptr == NULL || size == 0) {
        return;
    }

    volatile uint8_t* p = (volatile uint8_t*)ptr;
    while (size--) {
        *p++ = 0;
    }
}

bool secure_memcpy(void* dest, size_t dest_size, const void* src, size_t src_size) {
    if (dest == NULL || src == NULL || dest_size == 0 || src_size == 0) {
        return false;
    }

    if (src_size > dest_size) {
        ESP_LOGE(TAG, "Source size %zu exceeds destination size %zu", src_size, dest_size);
        return false;
    }

    memcpy(dest, src, src_size);
    return true;
}

/**
 * @brief Buffer validation with comprehensive security checks
 */
buffer_validation_result_t buffer_manager_secure_validate(size_t size_samples, const int16_t* data, size_t samples) {
    // Check for null pointers if data provided
    if (data == NULL && samples > 0) {
        return BUFFER_VALIDATION_NULL_POINTER;
    }

    // Validate size constraints
    if (size_samples < BUFFER_MIN_SIZE_SAMPLES) {
        return BUFFER_VALIDATION_SIZE_TOO_SMALL;
    }

    if (size_samples > BUFFER_MAX_SIZE_SAMPLES) {
        return BUFFER_VALIDATION_SIZE_TOO_LARGE;
    }

    // Check for power-of-2 alignment (optimization requirement)
    if ((size_samples & (size_samples - 1)) != 0) {
        ESP_LOGW(TAG, "Buffer size %zu is not power of 2, may impact performance", size_samples);
    }

    // Validate sample count if data provided
    if (samples > 0) {
        if (samples > size_samples) {
            return BUFFER_VALIDATION_OVERFLOW_RISK;
        }

        // Check for reasonable sample count (prevent integer overflow)
        if (samples > (BUFFER_MAX_SIZE_SAMPLES / 2)) {
            return BUFFER_VALIDATION_OVERFLOW_RISK;
        }
    }

    return BUFFER_VALIDATION_OK;
}

bool buffer_manager_secure_init(size_t size_samples) {
    if (initialized) {
        ESP_LOGW(TAG, "Buffer manager already initialized");
        return true;
    }

    // Validate parameters
    if (buffer_manager_secure_validate(size_samples, NULL, 0) != BUFFER_VALIDATION_OK) {
        ESP_LOGE(TAG, "Invalid buffer size: %zu", size_samples);
        return false;
    }

    ESP_LOGI(TAG, "Initializing secure buffer manager with %zu samples", size_samples);

    // Initialize synchronization primitives
    if (!priority_inheritance_mutex_create(&secure_buffer.access_mutex)) {
        ESP_LOGE(TAG, "Failed to create priority inheritance mutex");
        return false;
    }

    secure_buffer.stats_mutex = xSemaphoreCreateMutex();
    if (secure_buffer.stats_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create stats mutex");
        priority_inheritance_mutex_destroy(&secure_buffer.access_mutex);
        return false;
    }

    // Allocate buffer memory with security considerations
    size_t buffer_size_bytes = size_samples * sizeof(int16_t);
    secure_buffer.ring_buffer = (int16_t*)heap_caps_malloc(buffer_size_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    if (secure_buffer.ring_buffer == NULL) {
        // Fallback to internal SRAM if PSRAM not available
        secure_buffer.ring_buffer = (int16_t*)malloc(buffer_size_bytes);
        if (secure_buffer.ring_buffer == NULL) {
            ESP_LOGE(TAG, "Failed to allocate buffer memory");
            priority_inheritance_mutex_destroy(&secure_buffer.access_mutex);
            vSemaphoreDelete(secure_buffer.stats_mutex);
            return false;
        }
        ESP_LOGI(TAG, "Buffer allocated in internal SRAM");
    } else {
        ESP_LOGI(TAG, "Buffer allocated in PSRAM");
    }

    // Initialize buffer state
    secure_buffer.buffer_size_samples = size_samples;
    secure_buffer.buffer_size_bytes = buffer_size_bytes;
    secure_buffer.write_index = 0;
    secure_buffer.read_index = 0;
    secure_buffer.used_samples = 0;

    // Initialize statistics
    memset(&secure_buffer.stats, 0, sizeof(secure_buffer.stats));
    secure_buffer.stats.size_samples = size_samples;
    secure_buffer.stats.overflow_threshold = (size_samples * BUFFER_OVERFLOW_THRESHOLD) / 100;
    secure_buffer.stats.underflow_threshold = (size_samples * BUFFER_UNDERFLOW_THRESHOLD) / 100;

    // Initialize security features
    secure_buffer.overflow_detected = false;
    secure_buffer.underflow_detected = false;
    secure_buffer.overflow_threshold = (size_samples * BUFFER_OVERFLOW_THRESHOLD) / 100;
    secure_buffer.underflow_threshold = (size_samples * BUFFER_UNDERFLOW_THRESHOLD) / 100;

    secure_buffer.initialized = true;
    secure_buffer.initialization_magic = BUFFER_MAGIC;

    ESP_LOGI(TAG, "Secure buffer manager initialized successfully");
    return true;
}

bool buffer_manager_secure_overflow_risk(size_t requested_samples) {
    if (!initialized) {
        return false;
    }

    if (xSemaphoreTake(secure_buffer.stats_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return false;
    }

    size_t free_samples = secure_buffer.buffer_size_samples - secure_buffer.used_samples;
    bool risk = (requested_samples > free_samples) ||
                (secure_buffer.used_samples > secure_buffer.overflow_threshold);

    xSemaphoreGive(secure_buffer.stats_mutex);
    return risk;
}

uint8_t buffer_manager_secure_get_utilization_percent(void) {
    if (!initialized) {
        return 0;
    }

    if (xSemaphoreTake(secure_buffer.stats_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return 0;
    }

    uint8_t utilization = (secure_buffer.used_samples * 100) / secure_buffer.buffer_size_samples;

    // Update peak utilization
    if (utilization > secure_buffer.stats.peak_utilization_percent) {
        secure_buffer.stats.peak_utilization_percent = utilization;
    }

    xSemaphoreGive(secure_buffer.stats_mutex);
    return utilization;
}

size_t buffer_manager_secure_write(const int16_t* data, size_t samples) {
    if (!initialized || data == NULL || samples == 0) {
        return 0;
    }

    // Validate input parameters
    if (buffer_manager_secure_validate(secure_buffer.buffer_size_samples, data, samples) != BUFFER_VALIDATION_OK) {
        ESP_LOGE(TAG, "Invalid write parameters");
        return 0;
    }

    if (!priority_inheritance_mutex_take(&secure_buffer.access_mutex, pdMS_TO_TICKS(BUFFER_OPERATION_TIMEOUT_MS))) {
        ESP_LOGE(TAG, "Failed to acquire buffer mutex for write");
        if (xSemaphoreTake(secure_buffer.stats_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            secure_buffer.stats.timeout_count++;
            xSemaphoreGive(secure_buffer.stats_mutex);
        }
        return 0;
    }

    // Check for overflow risk
    if (buffer_manager_secure_overflow_risk(samples)) {
        ESP_LOGW(TAG, "Buffer overflow risk detected: requested %zu samples, available %zu",
                 samples, secure_buffer.buffer_size_samples - secure_buffer.used_samples);

        if (xSemaphoreTake(secure_buffer.stats_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            secure_buffer.stats.overflow_count++;
            secure_buffer.stats.last_overflow_tick = xTaskGetTickCount();
            secure_buffer.overflow_detected = true;
            xSemaphoreGive(secure_buffer.stats_mutex);
        }
    }

    // Calculate available space
    size_t free_samples = secure_buffer.buffer_size_samples - secure_buffer.used_samples;
    size_t samples_to_write = (samples > free_samples) ? free_samples : samples;

    if (samples_to_write == 0) {
        ESP_LOGW(TAG, "Buffer full, cannot write");
        priority_inheritance_mutex_give(&secure_buffer.access_mutex);
        return 0;
    }

    // Perform secure write with bounds checking
    size_t chunk1 = samples_to_write;
    size_t space_to_end = secure_buffer.buffer_size_samples - secure_buffer.write_index;

    if (chunk1 > space_to_end) {
        chunk1 = space_to_end;
    }

    // First chunk: write to end of buffer
    if (!secure_memcpy(&secure_buffer.ring_buffer[secure_buffer.write_index],
                       space_to_end * sizeof(int16_t),
                       data,
                       chunk1 * sizeof(int16_t))) {
        ESP_LOGE(TAG, "Secure memcpy failed for chunk 1");
        priority_inheritance_mutex_give(&secure_buffer.access_mutex);
        return 0;
    }

    // Second chunk: wrap around to beginning if needed
    size_t chunk2 = samples_to_write - chunk1;
    if (chunk2 > 0) {
        if (!secure_memcpy(secure_buffer.ring_buffer,
                           secure_buffer.buffer_size_samples * sizeof(int16_t),
                           &data[chunk1],
                           chunk2 * sizeof(int16_t))) {
            ESP_LOGE(TAG, "Secure memcpy failed for chunk 2");
            priority_inheritance_mutex_give(&secure_buffer.access_mutex);
            return 0;
        }
    }

    // Update indices and counters
    secure_buffer.write_index = (secure_buffer.write_index + samples_to_write) % secure_buffer.buffer_size_samples;
    secure_buffer.used_samples += samples_to_write;

    // Update statistics
    if (xSemaphoreTake(secure_buffer.stats_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        secure_buffer.stats.total_bytes_written += samples_to_write * sizeof(int16_t);
        secure_buffer.stats.write_index = secure_buffer.write_index;
        secure_buffer.stats.used_samples = secure_buffer.used_samples;

        // Update utilization statistics
        uint8_t current_util = (secure_buffer.used_samples * 100) / secure_buffer.buffer_size_samples;
        secure_buffer.stats.average_utilization =
            (secure_buffer.stats.average_utilization * 0.95f) + (current_util * 0.05f);

        if (current_util > secure_buffer.stats.peak_utilization_percent) {
            secure_buffer.stats.peak_utilization_percent = current_util;
        }

        xSemaphoreGive(secure_buffer.stats_mutex);
    }

    priority_inheritance_mutex_give(&secure_buffer.access_mutex);

    ESP_LOGD(TAG, "Written %zu samples, buffer now %zu/%zu used (%.1f%%)",
             samples_to_write, secure_buffer.used_samples, secure_buffer.buffer_size_samples,
             (float)(secure_buffer.used_samples * 100) / secure_buffer.buffer_size_samples);

    return samples_to_write;
}

size_t buffer_manager_secure_read(int16_t* data, size_t samples) {
    if (!initialized || data == NULL || samples == 0) {
        return 0;
    }

    // Validate input parameters
    if (buffer_manager_secure_validate(secure_buffer.buffer_size_samples, data, samples) != BUFFER_VALIDATION_OK) {
        ESP_LOGE(TAG, "Invalid read parameters");
        return 0;
    }

    if (!priority_inheritance_mutex_take(&secure_buffer.access_mutex, pdMS_TO_TICKS(BUFFER_OPERATION_TIMEOUT_MS))) {
        ESP_LOGE(TAG, "Failed to acquire buffer mutex for read");
        if (xSemaphoreTake(secure_buffer.stats_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            secure_buffer.stats.timeout_count++;
            xSemaphoreGive(secure_buffer.stats_mutex);
        }
        return 0;
    }

    // Check for underflow
    if (secure_buffer.used_samples == 0) {
        ESP_LOGW(TAG, "Buffer empty, cannot read");

        if (xSemaphoreTake(secure_buffer.stats_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            secure_buffer.stats.underflow_count++;
            secure_buffer.stats.last_underflow_tick = xTaskGetTickCount();
            secure_buffer.underflow_detected = true;
            xSemaphoreGive(secure_buffer.stats_mutex);
        }

        priority_inheritance_mutex_give(&secure_buffer.access_mutex);
        return 0;
    }

    // Calculate available data
    size_t samples_to_read = (samples > secure_buffer.used_samples) ? secure_buffer.used_samples : samples;

    // Perform secure read with bounds checking
    size_t chunk1 = samples_to_read;
    size_t space_to_end = secure_buffer.buffer_size_samples - secure_buffer.read_index;

    if (chunk1 > space_to_end) {
        chunk1 = space_to_end;
    }

    // First chunk: read to end of buffer
    if (!secure_memcpy(data,
                       samples * sizeof(int16_t),
                       &secure_buffer.ring_buffer[secure_buffer.read_index],
                       chunk1 * sizeof(int16_t))) {
        ESP_LOGE(TAG, "Secure memcpy failed for read chunk 1");
        priority_inheritance_mutex_give(&secure_buffer.access_mutex);
        return 0;
    }

    // Second chunk: wrap around if needed
    size_t chunk2 = samples_to_read - chunk1;
    if (chunk2 > 0) {
        if (!secure_memcpy(&data[chunk1],
                           (samples - chunk1) * sizeof(int16_t),
                           secure_buffer.ring_buffer,
                           chunk2 * sizeof(int16_t))) {
            ESP_LOGE(TAG, "Secure memcpy failed for read chunk 2");
            priority_inheritance_mutex_give(&secure_buffer.access_mutex);
            return 0;
        }
    }

    // Update indices and counters
    secure_buffer.read_index = (secure_buffer.read_index + samples_to_read) % secure_buffer.buffer_size_samples;
    secure_buffer.used_samples -= samples_to_read;

    // Update statistics
    if (xSemaphoreTake(secure_buffer.stats_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        secure_buffer.stats.total_bytes_read += samples_to_read * sizeof(int16_t);
        secure_buffer.stats.read_index = secure_buffer.read_index;
        secure_buffer.stats.used_samples = secure_buffer.used_samples;

        // Update utilization statistics
        uint8_t current_util = (secure_buffer.used_samples * 100) / secure_buffer.buffer_size_samples;
        secure_buffer.stats.average_utilization =
            (secure_buffer.stats.average_utilization * 0.95f) + (current_util * 0.05f);

        xSemaphoreGive(secure_buffer.stats_mutex);
    }

    priority_inheritance_mutex_give(&secure_buffer.access_mutex);

    ESP_LOGD(TAG, "Read %zu samples, buffer now %zu/%zu used (%.1f%%)",
             samples_to_read, secure_buffer.used_samples, secure_buffer.buffer_size_samples,
             (float)(secure_buffer.used_samples * 100) / secure_buffer.buffer_size_samples);

    return samples_to_read;
}

bool buffer_manager_secure_resize(size_t new_size_samples) {
    if (!initialized) {
        ESP_LOGE(TAG, "Buffer manager not initialized");
        return false;
    }

    // Validate new size
    if (buffer_manager_secure_validate(new_size_samples, NULL, 0) != BUFFER_VALIDATION_OK) {
        ESP_LOGE(TAG, "Invalid resize size: %zu", new_size_samples);
        return false;
    }

    // Cannot resize to smaller than current usage
    if (new_size_samples < secure_buffer.used_samples) {
        ESP_LOGE(TAG, "Cannot resize to %zu samples, currently using %zu samples",
                 new_size_samples, secure_buffer.used_samples);
        return false;
    }

    ESP_LOGI(TAG, "Resizing buffer from %zu to %zu samples",
             secure_buffer.buffer_size_samples, new_size_samples);

    if (!priority_inheritance_mutex_take(&secure_buffer.access_mutex, pdMS_TO_TICKS(BUFFER_RESIZE_TIMEOUT_MS))) {
        ESP_LOGE(TAG, "Failed to acquire buffer mutex for resize");
        return false;
    }

    size_t new_size_bytes = new_size_samples * sizeof(int16_t);
    int16_t* new_buffer = (int16_t*)heap_caps_malloc(new_size_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    if (new_buffer == NULL) {
        // Fallback to internal SRAM
        new_buffer = (int16_t*)malloc(new_size_bytes);
        if (new_buffer == NULL) {
            ESP_LOGE(TAG, "Failed to allocate memory for buffer resize");
            priority_inheritance_mutex_give(&secure_buffer.access_mutex);
            return false;
        }
        ESP_LOGI(TAG, "Resized buffer allocated in internal SRAM");
    }

    // Copy existing data to new buffer
    if (secure_buffer.used_samples > 0) {
        // If data doesn't wrap around, single copy is sufficient
        if (secure_buffer.write_index >= secure_buffer.read_index) {
            size_t contiguous_samples = secure_buffer.used_samples;
            if (!secure_memcpy(new_buffer, new_size_bytes,
                               &secure_buffer.ring_buffer[secure_buffer.read_index],
                               contiguous_samples * sizeof(int16_t))) {
                ESP_LOGE(TAG, "Failed to copy buffer data during resize");
                free(new_buffer);
                priority_inheritance_mutex_give(&secure_buffer.access_mutex);
                return false;
            }

            secure_buffer.read_index = 0;
            secure_buffer.write_index = contiguous_samples;
        } else {
            // Data wraps around, need two copies
            size_t first_chunk = secure_buffer.buffer_size_samples - secure_buffer.read_index;
            size_t second_chunk = secure_buffer.used_samples - first_chunk;

            if (!secure_memcpy(new_buffer, new_size_bytes,
                               &secure_buffer.ring_buffer[secure_buffer.read_index],
                               first_chunk * sizeof(int16_t))) {
                ESP_LOGE(TAG, "Failed to copy first chunk during resize");
                free(new_buffer);
                priority_inheritance_mutex_give(&secure_buffer.access_mutex);
                return false;
            }

            if (!secure_memcpy(&new_buffer[first_chunk], new_size_bytes - (first_chunk * sizeof(int16_t)),
                               secure_buffer.ring_buffer,
                               second_chunk * sizeof(int16_t))) {
                ESP_LOGE(TAG, "Failed to copy second chunk during resize");
                free(new_buffer);
                priority_inheritance_mutex_give(&secure_buffer.access_mutex);
                return false;
            }

            secure_buffer.read_index = 0;
            secure_buffer.write_index = secure_buffer.used_samples;
        }
    } else {
        // No data in buffer, reset indices
        secure_buffer.read_index = 0;
        secure_buffer.write_index = 0;
    }

    // Clear old buffer securely
    if (secure_buffer.ring_buffer != NULL) {
        secure_memzero(secure_buffer.ring_buffer, secure_buffer.buffer_size_bytes);
        free(secure_buffer.ring_buffer);
    }

    // Update buffer state
    secure_buffer.ring_buffer = new_buffer;
    secure_buffer.buffer_size_samples = new_size_samples;
    secure_buffer.buffer_size_bytes = new_size_bytes;

    // Update overflow thresholds
    secure_buffer.overflow_threshold = (new_size_samples * BUFFER_OVERFLOW_THRESHOLD) / 100;
    secure_buffer.underflow_threshold = (new_size_samples * BUFFER_UNDERFLOW_THRESHOLD) / 100;

    // Update statistics
    if (xSemaphoreTake(secure_buffer.stats_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        secure_buffer.stats.size_samples = new_size_samples;
        secure_buffer.stats.resize_count++;
        secure_buffer.stats.overflow_threshold = secure_buffer.overflow_threshold;
        secure_buffer.stats.underflow_threshold = secure_buffer.underflow_threshold;
        xSemaphoreGive(secure_buffer.stats_mutex);
    }

    priority_inheritance_mutex_give(&secure_buffer.access_mutex);

    ESP_LOGI(TAG, "Buffer resized successfully to %zu samples", new_size_samples);
    return true;
}

bool buffer_manager_secure_get_stats(secure_buffer_stats_t* stats) {
    if (stats == NULL || !initialized) {
        return false;
    }

    if (xSemaphoreTake(secure_buffer.stats_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return false;
    }

    memcpy(stats, &secure_buffer.stats, sizeof(secure_buffer_stats_t));

    xSemaphoreGive(secure_buffer.stats_mutex);
    return true;
}

bool buffer_manager_secure_reset(void) {
    if (!initialized) {
        return false;
    }

    if (!priority_inheritance_mutex_take(&secure_buffer.access_mutex, pdMS_TO_TICKS(BUFFER_OPERATION_TIMEOUT_MS))) {
        return false;
    }

    // Securely clear buffer memory
    if (secure_buffer.ring_buffer != NULL) {
        secure_memzero(secure_buffer.ring_buffer, secure_buffer.buffer_size_bytes);
    }

    // Reset state
    secure_buffer.write_index = 0;
    secure_buffer.read_index = 0;
    secure_buffer.used_samples = 0;
    secure_buffer.overflow_detected = false;
    secure_buffer.underflow_detected = false;

    // Reset statistics (preserve totals and configuration)
    uint64_t total_written = secure_buffer.stats.total_bytes_written;
    uint64_t total_read = secure_buffer.stats.total_bytes_read;
    uint32_t resize_count = secure_buffer.stats.resize_count;

    memset(&secure_buffer.stats, 0, sizeof(secure_buffer_stats_t));

    secure_buffer.stats.size_samples = secure_buffer.buffer_size_samples;
    secure_buffer.stats.total_bytes_written = total_written;
    secure_buffer.stats.total_bytes_read = total_read;
    secure_buffer.stats.resize_count = resize_count;
    secure_buffer.stats.overflow_threshold = secure_buffer.overflow_threshold;
    secure_buffer.stats.underflow_threshold = secure_buffer.underflow_threshold;

    priority_inheritance_mutex_give(&secure_buffer.access_mutex);

    ESP_LOGI(TAG, "Buffer reset completed");
    return true;
}

void buffer_manager_secure_deinit(void) {
    if (!initialized) {
        return;
    }

    ESP_LOGI(TAG, "Deinitializing secure buffer manager");

    // Acquire all mutexes for cleanup
    priority_inheritance_mutex_take(&secure_buffer.access_mutex, portMAX_DELAY);
    xSemaphoreTake(secure_buffer.stats_mutex, portMAX_DELAY);

    // Securely clear buffer memory before deallocation
    if (secure_buffer.ring_buffer != NULL) {
        secure_memzero(secure_buffer.ring_buffer, secure_buffer.buffer_size_bytes);
        free(secure_buffer.ring_buffer);
        secure_buffer.ring_buffer = NULL;
    }

    // Destroy synchronization primitives
    priority_inheritance_mutex_destroy(&secure_buffer.access_mutex);
    vSemaphoreDelete(secure_buffer.stats_mutex);

    // Clear sensitive data
    secure_memzero(&secure_buffer, sizeof(secure_buffer));

    initialized = false;
    ESP_LOGI(TAG, "Secure buffer manager deinitialized");
}

/**
 * @brief Compatibility wrapper for original buffer manager interface
 */
bool buffer_manager_init(size_t size) {
    return buffer_manager_secure_init(size);
}

size_t buffer_manager_write(const int16_t* data, size_t samples) {
    return buffer_manager_secure_write(data, samples);
}

size_t buffer_manager_read(int16_t* data, size_t samples) {
    return buffer_manager_secure_read(data, samples);
}

bool buffer_manager_resize(size_t new_size) {
    return buffer_manager_secure_resize(new_size);
}

void buffer_manager_get_stats(buffer_stats_t* stats) {
    if (stats == NULL) {
        return;
    }

    secure_buffer_stats_t secure_stats;
    if (buffer_manager_secure_get_stats(&secure_stats)) {
        // Convert secure stats to original format
        stats->size_samples = secure_stats.size_samples;
        stats->used_samples = secure_stats.used_samples;
        stats->write_index = secure_stats.write_index;
        stats->read_index = secure_stats.read_index;
        stats->overflow_count = secure_stats.overflow_count;
        stats->underflow_count = secure_stats.underflow_count;
        stats->resize_count = secure_stats.resize_count;
    }
}

void buffer_manager_deinit(void) {
    buffer_manager_secure_deinit();
}

size_t buffer_manager_get_size(void) {
    if (!initialized) {
        return 0;
    }
    return secure_buffer.buffer_size_samples;
}

size_t buffer_manager_get_used_samples(void) {
    if (!initialized) {
        return 0;
    }
    return secure_buffer.used_samples;
}

bool buffer_manager_is_full(void) {
    if (!initialized) {
        return false;
    }
    return secure_buffer.used_samples >= secure_buffer.buffer_size_samples;
}

bool buffer_manager_is_empty(void) {
    if (!initialized) {
        return true;
    }
    return secure_buffer.used_samples == 0;
}

size_t buffer_manager_get_free_samples(void) {
    if (!initialized) {
        return 0;
    }
    return secure_buffer.buffer_size_samples - secure_buffer.used_samples;
}

uint8_t buffer_manager_get_usage_percent(void) {
    return buffer_manager_secure_get_utilization_percent();
}

bool buffer_manager_resize_if_needed(void) {
    if (!initialized) {
        return false;
    }

    uint8_t utilization = buffer_manager_secure_get_utilization_percent();

    // Resize up if utilization is high
    if (utilization > 90 && secure_buffer.buffer_size_samples < BUFFER_MAX_SIZE_SAMPLES) {
        size_t new_size = secure_buffer.buffer_size_samples * 2;
        if (new_size > BUFFER_MAX_SIZE_SAMPLES) {
            new_size = BUFFER_MAX_SIZE_SAMPLES;
        }
        return buffer_manager_secure_resize(new_size);
    }

    // Resize down if utilization is very low
    if (utilization < 10 && secure_buffer.buffer_size_samples > BUFFER_MIN_SIZE_SAMPLES * 4) {
        size_t new_size = secure_buffer.buffer_size_samples / 2;
        if (new_size < BUFFER_MIN_SIZE_SAMPLES * 2) {
            new_size = BUFFER_MIN_SIZE_SAMPLES * 2;
        }
        return buffer_manager_secure_resize(new_size);
    }

    return false;
}

bool buffer_manager_clear(void) {
    return buffer_manager_secure_reset();
}

bool buffer_manager_will_overflow_if_written(size_t samples) {
    return buffer_manager_secure_overflow_risk(samples);
}