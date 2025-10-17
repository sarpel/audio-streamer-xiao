/**
 * @file i2s_handler_secure.cpp
 * @brief Secure I2S handler implementation with timeout protection
 * @author Security Implementation
 * @date 2025
 *
 * Enhanced I2S handler with timeout-based reading, overflow protection,
 * and comprehensive error handling to prevent audio dropouts and system hangs.
 */

#include "i2s_handler_secure.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s.h"
#include "driver/gpio.h"
#include "string.h"
#include "math.h"

static const char* TAG = "I2S_HANDLER_SECURE";

// Global I2S state with enhanced security
static struct {
    i2s_chan_handle_t rx_channel;
    uint32_t sample_rate;
    i2s_data_bit_width_t bits_per_sample;
    uint32_t channels;

    // Security and performance configuration
    i2s_adaptive_config_t adaptive_config;
    i2s_secure_stats_t stats;

    // State tracking
    bool initialized;
    uint32_t initialization_magic;
    TickType_t last_successful_read;
    uint32_t consecutive_failures;
    bool emergency_stop_active;

    // Performance monitoring
    uint64_t performance_start_time;
    uint32_t performance_sample_count;
    float performance_accumulated_time;
} secure_i2s = {0};

#define I2S_MAGIC 0x49325348  // "I2SH" in hex
#define I2S_DMA_BUFFER_COUNT 8
#define I2S_DMA_BUFFER_SIZE 512

/**
 * @brief I2S configuration with security enhancements
 */
static const i2s_std_config_t i2s_secure_config = {
    .clk_cfg = {
        .sample_rate_hz = 16000,  // Will be updated dynamically
        .clk_src = I2S_CLK_SRC_DEFAULT,
        .mclk_multiple = I2S_MCLK_MULTIPLE_256,
    },
    .slot_cfg = {
        .data_bit_width = I2S_DATA_BIT_WIDTH_24BIT,
        .slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT,
        .slot_mode = I2S_SLOT_MODE_MONO,
        .slot_mask = I2S_STD_SLOT_LEFT,
        .ws_width = 32,
        .ws_pol = false,
        .bit_shift = true,
        .left_align = true,
        .big_endian = false,
        .bit_order_lsb = false,
    },
    .gpio_cfg = {
        .mclk = I2S_GPIO_UNUSED,
        .bclk = GPIO_NUM_4,     // SCK
        .ws = GPIO_NUM_3,       // WS
        .dout = I2S_GPIO_UNUSED,
        .din = GPIO_NUM_2,      // SD
        .invert_flags = {
            .mclk_inv = false,
            .bclk_inv = false,
            .ws_inv = false,
        },
    },
};

/**
 * @brief Default adaptive configuration
 */
static const i2s_adaptive_config_t default_adaptive_config = {
    .enable_adaptive_timeout = true,
    .enable_overflow_protection = true,
    .enable_performance_monitoring = true,
    .base_timeout_ms = I2S_READ_TIMEOUT_MS,
    .overflow_threshold = I2S_OVERFLOW_THRESHOLD,
    .max_consecutive_failures = I2S_MAX_CONSECUTIVE_FAILURES,
};

/**
 * @brief Validate I2S configuration parameters
 */
bool i2s_handler_secure_validate_config(uint32_t sample_rate, i2s_data_bit_width_t bits_per_sample, uint32_t channels) {
    // Validate sample rate
    if (sample_rate < I2S_MIN_SAMPLE_RATE || sample_rate > I2S_MAX_SAMPLE_RATE) {
        ESP_LOGE(TAG, "Invalid sample rate: %u (must be %u-%u)",
                 sample_rate, I2S_MIN_SAMPLE_RATE, I2S_MAX_SAMPLE_RATE);
        return false;
    }

    // Validate bits per sample
    if (bits_per_sample != I2S_DATA_BIT_WIDTH_16BIT &&
        bits_per_sample != I2S_DATA_BIT_WIDTH_24BIT &&
        bits_per_sample != I2S_DATA_BIT_WIDTH_32BIT) {
        ESP_LOGE(TAG, "Invalid bits per sample: %d", bits_per_sample);
        return false;
    }

    // Validate channels
    if (channels != 1 && channels != 2) {
        ESP_LOGE(TAG, "Invalid channel count: %u (must be 1 or 2)", channels);
        return false;
    }

    return true;
}

/**
 * @brief Check if I2S reset is needed based on failure conditions
 */
bool i2s_handler_secure_reset_needed(void) {
    if (!secure_i2s.initialized) {
        return false;
    }

    // Check consecutive failures
    if (secure_i2s.consecutive_failures >= secure_i2s.adaptive_config.max_consecutive_failures) {
        ESP_LOGW(TAG, "Reset needed: %u consecutive failures", secure_i2s.consecutive_failures);
        return true;
    }

    // Check timeout frequency
    TickType_t current_tick = xTaskGetTickCount();
    if (secure_i2s.stats.timeout_count > 100 &&
        (current_tick - secure_i2s.stats.last_timeout_tick) < pdMS_TO_TICKS(10000)) {
        ESP_LOGW(TAG, "Reset needed: frequent timeouts");
        return true;
    }

    // Check if no successful reads for extended period
    if ((current_tick - secure_i2s.last_successful_read) > pdMS_TO_TICKS(30000)) {
        ESP_LOGW(TAG, "Reset needed: no successful reads for 30 seconds");
        return true;
    }

    return false;
}

/**
 * @brief Calculate adaptive timeout based on system conditions
 */
uint32_t i2s_handler_secure_calculate_adaptive_timeout(uint32_t base_timeout_ms) {
    if (!secure_i2s.adaptive_config.enable_adaptive_timeout) {
        return base_timeout_ms;
    }

    uint32_t timeout = base_timeout_ms;

    // Adjust based on consecutive failures
    if (secure_i2s.consecutive_failures > 0) {
        timeout = base_timeout_ms * (1 + (secure_i2s.consecutive_failures * 0.1f));
        if (timeout > I2S_ADAPTIVE_TIMEOUT_MAX_MS) {
            timeout = I2S_ADAPTIVE_TIMEOUT_MAX_MS;
        }
    }

    // Adjust based on buffer utilization
    uint8_t buffer_util = 0; // This would come from buffer manager
    if (buffer_util > 80) {
        timeout = timeout * 0.8f; // Reduce timeout under high load
    }

    // Ensure minimum timeout
    if (timeout < I2S_ADAPTIVE_TIMEOUT_MIN_MS) {
        timeout = I2S_ADAPTIVE_TIMEOUT_MIN_MS;
    }

    ESP_LOGD(TAG, "Adaptive timeout: base=%u, calculated=%u", base_timeout_ms, timeout);
    return timeout;
}

/**
 * @brief Monitor I2S performance and adjust parameters
 */
bool i2s_handler_secure_monitor_performance(void) {
    if (!secure_i2s.initialized || !secure_i2s.adaptive_config.enable_performance_monitoring) {
        return false;
    }

    // Calculate current performance metrics
    uint32_t current_time_ms = esp_timer_get_time() / 1000;
    uint32_t window_duration_ms = current_time_ms - secure_i2s.performance_start_time;

    if (window_duration_ms >= 1000 && secure_i2s.performance_sample_count > 0) { // 1 second window
        float avg_read_time_us = secure_i2s.performance_accumulated_time / secure_i2s.performance_sample_count;

        // Update statistics
        secure_i2s.stats.average_read_time_us = avg_read_time_us;
        if (avg_read_time_us > secure_i2s.stats.peak_read_time_us) {
            secure_i2s.stats.peak_read_time_us = avg_read_time_us;
        }

        // Check for performance degradation
        if (avg_read_time_us > 1000) { // 1ms threshold
            ESP_LOGW(TAG, "Performance degradation detected: avg read time = %.1f us", avg_read_time_us);

            // Adjust parameters if needed
            if (secure_i2s.adaptive_config.enable_adaptive_timeout) {
                secure_i2s.adaptive_config.base_timeout_ms = I2S_READ_TIMEOUT_MS * 1.5f;
                ESP_LOGI(TAG, "Increased base timeout to %u ms", secure_i2s.adaptive_config.base_timeout_ms);
            }
        }

        // Reset performance monitoring
        secure_i2s.performance_start_time = current_time_ms;
        secure_i2s.performance_sample_count = 0;
        secure_i2s.performance_accumulated_time = 0;
    }

    return true;
}

/**
 * @brief Check for I2S buffer overflow risk
 */
bool i2s_handler_secure_overflow_risk(size_t requested_samples) {
    if (!secure_i2s.initialized || !secure_i2s.adaptive_config.enable_overflow_protection) {
        return false;
    }

    // This would integrate with buffer manager to check current utilization
    // For now, use a simple heuristic based on consecutive failures
    if (secure_i2s.consecutive_failures > 3) {
        return true;
    }

    return false;
}

/**
 * @brief Emergency I2S stop for critical situations
 */
bool i2s_handler_secure_emergency_stop(void) {
    if (!secure_i2s.initialized) {
        return false;
    }

    ESP_LOGW(TAG, "Emergency I2S stop requested");
    secure_i2s.emergency_stop_active = true;

    // Stop I2S channel
    esp_err_t ret = i2s_channel_disable(secure_i2s.rx_channel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to disable I2S channel during emergency stop: %s", esp_err_to_name(ret));
        return false;
    }

    // Log emergency event
    ESP_LOGW(TAG, "I2S emergency stop completed");
    return true;
}

/**
 * @brief Initialize secure I2S handler
 */
bool i2s_handler_secure_init(uint32_t sample_rate, i2s_data_bit_width_t bits_per_sample, uint32_t channels) {
    if (secure_i2s.initialized) {
        ESP_LOGW(TAG, "I2S handler already initialized");
        return true;
    }

    // Validate configuration
    if (!i2s_handler_secure_validate_config(sample_rate, bits_per_sample, channels)) {
        return false;
    }

    ESP_LOGI(TAG, "Initializing secure I2S handler: %u Hz, %d bits, %u channels",
             sample_rate, bits_per_sample, channels);

    // Initialize I2S peripheral
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = I2S_DMA_BUFFER_COUNT;
    chan_cfg.dma_frame_num = I2S_DMA_BUFFER_SIZE;
    chan_cfg.auto_clear = true; // Auto clear on descriptor error

    esp_err_t ret = i2s_new_channel(&chan_cfg, NULL, &secure_i2s.rx_channel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2S channel: %s", esp_err_to_name(ret));
        return false;
    }

    // Configure I2S with security enhancements
    i2s_std_config_t std_cfg = i2s_secure_config;
    std_cfg.clk_cfg.sample_rate_hz = sample_rate;
    std_cfg.slot_cfg.data_bit_width = bits_per_sample;

    ret = i2s_channel_init_std_mode(secure_i2s.rx_channel, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize I2S channel: %s", esp_err_to_name(ret));
        i2s_del_channel(secure_i2s.rx_channel);
        return false;
    }

    // Enable I2S channel
    ret = i2s_channel_enable(secure_i2s.rx_channel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable I2S channel: %s", esp_err_to_name(ret));
        i2s_del_channel(secure_i2s.rx_channel);
        return false;
    }

    // Initialize state
    secure_i2s.sample_rate = sample_rate;
    secure_i2s.bits_per_sample = bits_per_sample;
    secure_i2s.channels = channels;
    secure_i2s.adaptive_config = default_adaptive_config;
    secure_i2s.last_successful_read = xTaskGetTickCount();
    secure_i2s.performance_start_time = esp_timer_get_time() / 1000;
    secure_i2s.initialized = true;
    secure_i2s.initialization_magic = I2S_MAGIC;

    ESP_LOGI(TAG, "Secure I2S handler initialized successfully");
    return true;
}

/**
 * @brief Read audio samples with timeout protection (main function)
 */
size_t i2s_handler_secure_read_timeout(int16_t* out, int32_t* tmp_buffer, size_t samples, uint32_t timeout_ms) {
    if (!secure_i2s.initialized || out == NULL || tmp_buffer == NULL || samples == 0) {
        return 0;
    }

    // Check for emergency stop
    if (secure_i2s.emergency_stop_active) {
        ESP_LOGW(TAG, "I2S read blocked due to emergency stop");
        return 0;
    }

    // Check for overflow risk
    if (i2s_handler_secure_overflow_risk(samples)) {
        ESP_LOGW(TAG, "I2S overflow risk detected, throttling read");
        vTaskDelay(pdMS_TO_TICKS(10)); // Small delay to allow buffer to drain
        return 0;
    }

    // Calculate adaptive timeout if needed
    if (timeout_ms == 0) {
        timeout_ms = i2s_handler_secure_calculate_adaptive_timeout(secure_i2s.adaptive_config.base_timeout_ms);
    }

    // Performance monitoring start
    uint64_t read_start_time = esp_timer_get_time();

    // Read raw I2S data with timeout
    size_t bytes_read = 0;
    size_t bytes_to_read = samples * sizeof(int32_t);

    esp_err_t ret = i2s_channel_read(secure_i2s.rx_channel, tmp_buffer, bytes_to_read, &bytes_read, pdMS_TO_TICKS(timeout_ms));

    // Performance monitoring end
    uint64_t read_end_time = esp_timer_get_time();
    float read_time_us = (float)(read_end_time - read_start_time);

    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "I2S read failed: %s", esp_err_to_name(ret));
        secure_i2s.consecutive_failures++;
        secure_i2s.stats.timeout_count++;
        secure_i2s.stats.last_timeout_tick = xTaskGetTickCount();

        // Monitor performance and check if reset needed
        i2s_handler_secure_monitor_performance();

        if (i2s_handler_secure_reset_needed()) {
            ESP_LOGW(TAG, "I2S reset needed due to failures");
            i2s_handler_secure_reset();
        }

        return 0;
    }

    if (bytes_read == 0) {
        ESP_LOGW(TAG, "I2S read timeout: no data available");
        secure_i2s.consecutive_failures++;
        secure_i2s.stats.timeout_count++;
        secure_i2s.stats.last_timeout_tick = xTaskGetTickCount();
        return 0;
    }

    // Success - reset failure counter and update statistics
    secure_i2s.consecutive_failures = 0;
    secure_i2s.last_successful_read = xTaskGetTickCount();

    size_t samples_read = bytes_read / sizeof(int32_t);

    // Update performance statistics
    secure_i2s.performance_sample_count++;
    secure_i2s.performance_accumulated_time += read_time_us;
    secure_i2s.stats.total_samples_read += samples_read;

    // Update timing statistics
    if (read_time_us > secure_i2s.stats.peak_read_time_us) {
        secure_i2s.stats.peak_read_time_us = read_time_us;
    }

    // Convert 24-bit samples to 16-bit with saturation
    for (size_t i = 0; i < samples_read; i++) {
        int32_t sample = tmp_buffer[i];

        // Convert 24-bit to 16-bit by shifting right 8 bits with saturation
        int32_t shifted = sample >> 8;

        // Saturate to 16-bit range
        if (shifted > INT16_MAX) shifted = INT16_MAX;
        else if (shifted < INT16_MIN) shifted = INT16_MIN;

        out[i] = (int16_t)shifted;
    }

    // Update buffer utilization (would integrate with buffer manager)
    secure_i2s.stats.buffer_utilization_percent = 0; // Placeholder

    ESP_LOGD(TAG, "Read %zu samples in %.1f us (timeout: %u ms)",
             samples_read, read_time_us, timeout_ms);

    return samples_read;
}

/**
 * @brief Read audio samples with default timeout
 */
size_t i2s_handler_secure_read_16(int16_t* out, int32_t* tmp_buffer, size_t samples) {
    return i2s_handler_secure_read_timeout(out, tmp_buffer, samples, 0); // 0 = adaptive timeout
}

/**
 * @brief Get I2S handler statistics
 */
bool i2s_handler_secure_get_stats(i2s_secure_stats_t* stats) {
    if (stats == NULL || !secure_i2s.initialized) {
        return false;
    }

    memcpy(stats, &secure_i2s.stats, sizeof(i2s_secure_stats_t));
    stats->current_timeout_ms = secure_i2s.adaptive_config.base_timeout_ms;
    stats->is_adaptive_mode = secure_i2s.adaptive_config.enable_adaptive_timeout;
    stats->consecutive_failures = secure_i2s.consecutive_failures;

    return true;
}

/**
 * @brief Configure adaptive I2S behavior
 */
bool i2s_handler_secure_configure_adaptive(const i2s_adaptive_config_t* config) {
    if (config == NULL || !secure_i2s.initialized) {
        return false;
    }

    secure_i2s.adaptive_config = *config;
    ESP_LOGI(TAG, "Adaptive I2S configuration updated");
    return true;
}

/**
 * @brief Reset I2S handler after failures
 */
bool i2s_handler_secure_reset(void) {
    if (!secure_i2s.initialized) {
        return false;
    }

    TickType_t current_tick = xTaskGetTickCount();

    // Check cooldown period
    if ((current_tick - secure_i2s.stats.last_reset_tick) < pdMS_TO_TICKS(I2S_RESET_COOLDOWN_MS)) {
        ESP_LOGW(TAG, "I2S reset on cooldown, waiting...");
        vTaskDelay(pdMS_TO_TICKS(I2S_RESET_COOLDOWN_MS));
    }

    ESP_LOGW(TAG, "Resetting I2S handler after %u failures", secure_i2s.consecutive_failures);

    // Disable I2S channel
    esp_err_t ret = i2s_channel_disable(secure_i2s.rx_channel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to disable I2S channel during reset: %s", esp_err_to_name(ret));
        return false;
    }

    // Small delay for hardware stabilization
    vTaskDelay(pdMS_TO_TICKS(100));

    // Re-enable I2S channel
    ret = i2s_channel_enable(secure_i2s.rx_channel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable I2S channel after reset: %s", esp_err_to_name(ret));
        return false;
    }

    // Reset state
    secure_i2s.consecutive_failures = 0;
    secure_i2s.stats.last_reset_tick = current_tick;
    secure_i2s.stats.reset_count++;
    secure_i2s.last_successful_read = current_tick;

    ESP_LOGI(TAG, "I2S handler reset completed successfully");
    return true;
}

/**
 * @brief Deinitialize secure I2S handler
 */
void i2s_handler_secure_deinit(void) {
    if (!secure_i2s.initialized) {
        return;
    }

    ESP_LOGI(TAG, "Deinitializing secure I2S handler");

    if (secure_i2s.rx_channel != NULL) {
        i2s_channel_disable(secure_i2s.rx_channel);
        i2s_del_channel(secure_i2s.rx_channel);
        secure_i2s.rx_channel = NULL;
    }

    secure_i2s.initialized = false;
    secure_i2s.initialization_magic = 0;
    secure_i2s.emergency_stop_active = false;

    ESP_LOGI(TAG, "Secure I2S handler deinitialized");
}

/**
 * @brief Compatibility wrapper for original I2S interface
 */
size_t i2s_read_16(int16_t* out, int32_t* tmp_buffer, size_t samples) {
    return i2s_handler_secure_read_16(out, tmp_buffer, samples);
}

size_t i2s_read_24(int32_t* out, size_t samples) {
    if (!secure_i2s.initialized || out == NULL || samples == 0) {
        return 0;
    }

    size_t bytes_read = 0;
    size_t bytes_to_read = samples * sizeof(int32_t);

    esp_err_t ret = i2s_channel_read(secure_i2s.rx_channel, out, bytes_to_read, &bytes_read,
                                     pdMS_TO_TICKS(I2S_READ_TIMEOUT_MS));

    if (ret != ESP_OK || bytes_read == 0) {
        secure_i2s.consecutive_failures++;
        return 0;
    }

    secure_i2s.consecutive_failures = 0;
    secure_i2s.stats.total_samples_read += bytes_read / sizeof(int32_t);

    return bytes_read / sizeof(int32_t);
}

bool i2s_handler_init(uint32_t sample_rate, i2s_data_bit_width_t bits_per_sample, uint32_t channels) {
    return i2s_handler_secure_init(sample_rate, bits_per_sample, channels);
}

void i2s_handler_deinit(void) {
    i2s_handler_secure_deinit();
}