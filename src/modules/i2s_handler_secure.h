/**
 * @file i2s_handler_secure.h
 * @brief Secure I2S handler with timeout protection and overflow prevention
 * @author Security Implementation
 * @date 2025
 *
 * Enhanced I2S handler with timeout-based reading, overflow protection,
 * and comprehensive error handling to prevent audio dropouts and system hangs.
 */

#ifndef I2S_HANDLER_SECURE_H
#define I2S_HANDLER_SECURE_H

#include "stdint.h"
#include "stdbool.h"
#include "stddef.h"
#include "driver/i2s.h"

#ifdef __cplusplus
extern "C" {
#endif

// I2S Security Parameters
#define I2S_READ_TIMEOUT_MS 50           // Timeout for I2S read operations
#define I2S_OVERFLOW_THRESHOLD 90        // Buffer usage threshold for overflow protection
#define I2S_MAX_CONSECUTIVE_FAILURES 10  // Maximum consecutive failures before reset
#define I2S_RESET_COOLDOWN_MS 1000       // Cooldown period after I2S reset
#define I2S_ADAPTIVE_TIMEOUT_MIN_MS 10   // Minimum adaptive timeout
#define I2S_ADAPTIVE_TIMEOUT_MAX_MS 100  // Maximum adaptive timeout

// I2S Performance Monitoring
#define I2S_PERFORMANCE_WINDOW_SAMPLES 1000  // Samples window for performance metrics
#define I2S_MAX_SAMPLE_RATE 96000             // Maximum supported sample rate
#define I2S_MIN_SAMPLE_RATE 8000              // Minimum supported sample rate

/**
 * @brief I2S handler statistics with security metrics
 */
typedef struct {
    uint64_t total_samples_read;
    uint64_t total_samples_dropped;
    uint32_t timeout_count;
    uint32_t overflow_count;
    uint32_t reset_count;
    uint32_t consecutive_failures;
    float average_read_time_us;
    float peak_read_time_us;
    uint32_t current_sample_rate;
    uint8_t buffer_utilization_percent;
    TickType_t last_reset_tick;
    TickType_t last_timeout_tick;
    bool is_adaptive_mode;
    uint32_t current_timeout_ms;
} i2s_secure_stats_t;

/**
 * @brief I2S adaptive configuration
 */
typedef struct {
    bool enable_adaptive_timeout;    // Enable adaptive timeout based on system load
    bool enable_overflow_protection; // Enable overflow protection
    bool enable_performance_monitoring; // Enable detailed performance monitoring
    uint32_t base_timeout_ms;        // Base timeout value
    uint32_t overflow_threshold;     // Buffer usage threshold (0-100)
    uint32_t max_consecutive_failures; // Maximum failures before reset
} i2s_adaptive_config_t;

/**
 * @brief Initialize secure I2S handler
 * @param sample_rate Audio sample rate
 * @param bits_per_sample Bits per sample (16 recommended)
 * @param channels Number of channels (1 for mono)
 * @return true if initialization successful, false otherwise
 */
bool i2s_handler_secure_init(uint32_t sample_rate, i2s_data_bit_width_t bits_per_sample, uint32_t channels);

/**
 * @brief Read audio samples with timeout protection
 * @param out Output buffer for 16-bit samples
 * @param tmp_buffer Temporary buffer for raw I2S data
 * @param samples Number of samples to read
 * @return Number of samples actually read, 0 on timeout/error
 */
size_t i2s_handler_secure_read_16(int16_t* out, int32_t* tmp_buffer, size_t samples);

/**
 * @brief Read audio samples with adaptive timeout
 * @param out Output buffer for 16-bit samples
 * @param tmp_buffer Temporary buffer for raw I2S data
 * @param samples Number of samples to read
 * @param timeout_ms Timeout in milliseconds (0 for adaptive)
 * @return Number of samples actually read, 0 on timeout/error
 */
size_t i2s_handler_secure_read_timeout(int16_t* out, int32_t* tmp_buffer, size_t samples, uint32_t timeout_ms);

/**
 * @brief Check for I2S buffer overflow risk
 * @param requested_samples Number of samples to be read
 * @return true if overflow risk detected, false otherwise
 */
bool i2s_handler_secure_overflow_risk(size_t requested_samples);

/**
 * @brief Get I2S handler statistics
 * @param stats Pointer to statistics structure
 * @return true if statistics retrieved, false otherwise
 */
bool i2s_handler_secure_get_stats(i2s_secure_stats_t* stats);

/**
 * @brief Configure adaptive I2S behavior
 * @param config Adaptive configuration structure
 * @return true if configuration successful, false otherwise
 */
bool i2s_handler_secure_configure_adaptive(const i2s_adaptive_config_t* config);

/**
 * @brief Reset I2S handler after failures
 * @return true if reset successful, false otherwise
 */
bool i2s_handler_secure_reset(void);

/**
 * @brief Deinitialize secure I2S handler
 */
void i2s_handler_secure_deinit(void);

/**
 * @brief Monitor I2S performance and adjust parameters
 * @return true if monitoring successful, false otherwise
 */
bool i2s_handler_secure_monitor_performance(void);

/**
 * @brief Calculate adaptive timeout based on system conditions
 * @param base_timeout_ms Base timeout value
 * @return Calculated adaptive timeout
 */
uint32_t i2s_handler_secure_calculate_adaptive_timeout(uint32_t base_timeout_ms);

/**
 * @brief Validate I2S configuration parameters
 * @param sample_rate Sample rate to validate
 * @param bits_per_sample Bits per sample to validate
 * @param channels Number of channels to validate
 * @return true if configuration valid, false otherwise
 */
bool i2s_handler_secure_validate_config(uint32_t sample_rate, i2s_data_bit_width_t bits_per_sample, uint32_t channels);

/**
 * @brief Check if I2S reset is needed based on failure conditions
 * @return true if reset recommended, false otherwise
 */
bool i2s_handler_secure_reset_needed(void);

/**
 * @brief Emergency I2S stop for critical situations
 * @return true if stop successful, false otherwise
 */
bool i2s_handler_secure_emergency_stop(void);

/**
 * @brief Compatibility wrapper for original I2S interface
 */
size_t i2s_read_16(int16_t* out, int32_t* tmp_buffer, size_t samples);
size_t i2s_read_24(int32_t* out, size_t samples);
bool i2s_handler_init(uint32_t sample_rate, i2s_data_bit_width_t bits_per_sample, uint32_t channels);
void i2s_handler_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // I2S_HANDLER_SECURE_H