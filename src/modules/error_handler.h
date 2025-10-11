#ifndef ERROR_HANDLER_H
#define ERROR_HANDLER_H

#include <stdbool.h>
#include "esp_err.h"

/**
 * System error codes for unified error handling
 */
typedef enum {
    SYS_ERR_OK = 0,
    SYS_ERR_INIT_FAILED,
    SYS_ERR_NO_MEMORY,
    SYS_ERR_NETWORK_FAILED,
    SYS_ERR_INVALID_CONFIG,
    SYS_ERR_I2S_FAILURE,
    SYS_ERR_TCP_FAILURE,
    SYS_ERR_BUFFER_OVERFLOW,
    SYS_ERR_TIMEOUT
} system_error_t;

/**
 * Error severity levels
 */
typedef enum {
    ERR_SEVERITY_INFO,      // Informational, no action needed
    ERR_SEVERITY_WARNING,   // Warning, degraded operation
    ERR_SEVERITY_ERROR,     // Error, functionality impaired
    ERR_SEVERITY_CRITICAL,  // Critical, system restart required
    ERR_SEVERITY_FATAL      // Fatal, immediate restart
} error_severity_t;

/**
 * Initialize error handler
 *
 * @return true on success, false on failure
 */
bool error_handler_init(void);

/**
 * Log and handle system error
 *
 * @param err Error code
 * @param severity Error severity
 * @param module Module name (e.g., "I2S", "TCP", "BUFFER")
 * @param message Error message
 */
void system_error(system_error_t err, error_severity_t severity,
                  const char* module, const char* message);

/**
 * Fatal error handler - performs cleanup and restarts system
 *
 * @param err Error code
 * @param module Module name
 * @param message Error message
 */
void system_fatal_error(system_error_t err, const char* module, const char* message);

/**
 * Get error count for a specific error type
 *
 * @param err Error code
 * @return Number of occurrences since boot
 */
uint32_t error_handler_get_count(system_error_t err);

/**
 * Reset error counters
 */
void error_handler_reset_counters(void);

/**
 * Enable/disable automatic restart on critical errors
 *
 * @param enable true to enable auto-restart
 */
void error_handler_set_auto_restart(bool enable);

#endif // ERROR_HANDLER_H
