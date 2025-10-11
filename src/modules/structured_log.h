#ifndef STRUCTURED_LOG_H
#define STRUCTURED_LOG_H

#include "esp_log.h"

/**
 * Structured logging macros with context information
 *
 * These macros provide enhanced logging with function name and line number context
 */

#define LOG_CONTEXT(tag, level, fmt, ...) \
    ESP_LOG_LEVEL(level, tag, "[%s:%d] " fmt, __FUNCTION__, __LINE__, ##__VA_ARGS__)

// Convenience macros for each log level
#define LOG_ERROR_CTX(tag, fmt, ...) \
    LOG_CONTEXT(tag, ESP_LOG_ERROR, fmt, ##__VA_ARGS__)

#define LOG_WARN_CTX(tag, fmt, ...) \
    LOG_CONTEXT(tag, ESP_LOG_WARN, fmt, ##__VA_ARGS__)

#define LOG_INFO_CTX(tag, fmt, ...) \
    LOG_CONTEXT(tag, ESP_LOG_INFO, fmt, ##__VA_ARGS__)

#define LOG_DEBUG_CTX(tag, fmt, ...) \
    LOG_CONTEXT(tag, ESP_LOG_DEBUG, fmt, ##__VA_ARGS__)

// Network-specific structured logging
#define LOG_NET_ERROR(tag, operation, errno_val, fmt, ...) \
    ESP_LOGE(tag, "[%s:%d] %s failed: errno=%d, " fmt, \
             __FUNCTION__, __LINE__, operation, errno_val, ##__VA_ARGS__)

// Buffer operation structured logging
#define LOG_BUFFER_OP(tag, operation, size, available, fmt, ...) \
    ESP_LOGI(tag, "[%s:%d] %s: size=%zu, available=%zu, " fmt, \
             __FUNCTION__, __LINE__, operation, size, available, ##__VA_ARGS__)

// Task performance logging
#define LOG_TASK_PERF(tag, task_name, duration_ms, cpu_percent) \
    ESP_LOGI(tag, "[PERF] %s: duration=%lums, cpu=%lu%%", \
             task_name, duration_ms, cpu_percent)

#endif // STRUCTURED_LOG_H
