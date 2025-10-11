#include "buffer_manager.h"
#include "../config.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

static const char* TAG = "BUFFER_MANAGER";

// Mutex timeout constant (5 seconds) to prevent deadlocks
#define BUFFER_MUTEX_TIMEOUT_MS 5000

static int32_t* ring_buffer = NULL;
static size_t buffer_size_samples = 0;
static size_t read_index = 0;
static size_t write_index = 0;
static size_t available_samples = 0;
static SemaphoreHandle_t buffer_mutex = NULL;
static bool overflow_occurred = false;

bool buffer_manager_init(size_t size_bytes) {
    // Calculate size in samples (4 bytes per int32_t sample)
    buffer_size_samples = size_bytes / sizeof(int32_t);

    ESP_LOGI(TAG, "Initializing %d KB ring buffer (%d samples)",
             size_bytes / 1024, buffer_size_samples);

    // Try to allocate buffer in PSRAM first (if available)
    ring_buffer = (int32_t*)heap_caps_malloc(size_bytes, MALLOC_CAP_SPIRAM);
    if (ring_buffer == NULL) {
        // PSRAM not available, use internal SRAM
        ring_buffer = (int32_t*)malloc(size_bytes);
        if (ring_buffer == NULL) {
            ESP_LOGE(TAG, "Failed to allocate ring buffer");
            return false;
        }
        ESP_LOGI(TAG, "Ring buffer allocated in internal SRAM");
    } else {
        ESP_LOGI(TAG, "Ring buffer allocated in PSRAM");
    }

    // Create mutex for thread-safe access
    buffer_mutex = xSemaphoreCreateMutex();
    if (buffer_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create buffer mutex");
        free(ring_buffer);
        ring_buffer = NULL;
        return false;
    }

    // Initialize indices
    read_index = 0;
    write_index = 0;
    available_samples = 0;
    overflow_occurred = false;

    ESP_LOGI(TAG, "Buffer manager initialized successfully");
    return true;
}

size_t buffer_manager_write(const int32_t* data, size_t samples) {
    if (ring_buffer == NULL || data == NULL || samples == 0) {
        return 0;
    }

    // ✅ Add timeout instead of blocking forever
    if (xSemaphoreTake(buffer_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGE(TAG, "CRITICAL: Mutex timeout in write");
        return 0;  // Or trigger recovery
    }

    size_t free_space = buffer_size_samples - available_samples;
    size_t samples_to_write = samples;

    // Check for overflow
    if (samples_to_write > free_space) {
        ESP_LOGW(TAG, "Buffer overflow! Requested: %d, Available: %d", samples_to_write, free_space);
        samples_to_write = free_space;
        overflow_occurred = true;
    }

    // Write samples to ring buffer
    for (size_t i = 0; i < samples_to_write; i++) {
        ring_buffer[write_index] = data[i];
        write_index = (write_index + 1) % buffer_size_samples;
    }

    available_samples += samples_to_write;

    xSemaphoreGive(buffer_mutex);

    return samples_to_write;
}

size_t buffer_manager_read(int32_t* data, size_t samples) {
    if (ring_buffer == NULL || data == NULL || samples == 0) {
        return 0;
    }

    if (xSemaphoreTake(buffer_mutex, pdMS_TO_TICKS(BUFFER_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGE(TAG, "Mutex timeout in read");
        return 0;
    }

    size_t samples_to_read = (samples > available_samples) ? available_samples : samples;

    // Read samples from ring buffer
    for (size_t i = 0; i < samples_to_read; i++) {
        data[i] = ring_buffer[read_index];
        read_index = (read_index + 1) % buffer_size_samples;
    }

    available_samples -= samples_to_read;

    xSemaphoreGive(buffer_mutex);

    return samples_to_read;
}

size_t buffer_manager_available(void) {
    if (xSemaphoreTake(buffer_mutex, pdMS_TO_TICKS(BUFFER_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGE(TAG, "Mutex timeout in available");
        return 0;
    }
    size_t avail = available_samples;
    xSemaphoreGive(buffer_mutex);
    return avail;
}

size_t buffer_manager_free_space(void) {
    if (xSemaphoreTake(buffer_mutex, pdMS_TO_TICKS(BUFFER_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGE(TAG, "Mutex timeout in free_space");
        return 0;
    }
    size_t free_space = buffer_size_samples - available_samples;
    xSemaphoreGive(buffer_mutex);
    return free_space;
}

uint8_t buffer_manager_usage_percent(void) {
    if (xSemaphoreTake(buffer_mutex, pdMS_TO_TICKS(BUFFER_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGE(TAG, "Mutex timeout in usage_percent");
        return 0;
    }
    uint8_t usage = (available_samples * 100) / buffer_size_samples;
    xSemaphoreGive(buffer_mutex);
    return usage;
}

bool buffer_manager_check_overflow(void) {
    if (xSemaphoreTake(buffer_mutex, pdMS_TO_TICKS(BUFFER_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGE(TAG, "Mutex timeout in check_overflow");
        return false;
    }
    bool overflow = overflow_occurred;
    overflow_occurred = false;  // Reset flag after reading
    xSemaphoreGive(buffer_mutex);
    return overflow;
}

void buffer_manager_reset(void) {
    if (xSemaphoreTake(buffer_mutex, pdMS_TO_TICKS(BUFFER_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGE(TAG, "Mutex timeout in reset");
        return;
    }
    read_index = 0;
    write_index = 0;
    available_samples = 0;
    overflow_occurred = false;
    xSemaphoreGive(buffer_mutex);
    ESP_LOGI(TAG, "Buffer reset");
}

void buffer_manager_deinit(void) {
    if (buffer_mutex != NULL) {
        vSemaphoreDelete(buffer_mutex);
        buffer_mutex = NULL;
    }

    if (ring_buffer != NULL) {
        free(ring_buffer);
        ring_buffer = NULL;
    }

    ESP_LOGI(TAG, "Buffer manager deinitialized");
}
