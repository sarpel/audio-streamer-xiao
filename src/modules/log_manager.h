#ifndef LOG_MANAGER_H
#define LOG_MANAGER_H

#include <stddef.h>
#include <stdbool.h>
#include "esp_log.h"

#define MAX_LOG_ENTRIES 200
#define MAX_LOG_MESSAGE_LEN 128
#define MAX_LOG_TAG_LEN 16

typedef struct {
    uint32_t timestamp;     // Milliseconds since boot
    esp_log_level_t level;
    char tag[MAX_LOG_TAG_LEN];
    char message[MAX_LOG_MESSAGE_LEN];
} log_entry_t;

/**
 * Initialize log manager and hook into ESP-IDF logging system
 * @return true on success
 */
bool log_manager_init(void);

/**
 * Get all stored log entries
 * @param logs Array to store log entries
 * @param max_count Maximum number of logs to retrieve
 * @return Number of logs retrieved
 */
size_t log_manager_get_logs(log_entry_t* logs, size_t max_count);

/**
 * Get number of stored logs
 * @return Log count
 */
size_t log_manager_get_count(void);

/**
 * Clear all stored logs
 */
void log_manager_clear(void);

/**
 * Get log level as string
 * @param level ESP log level
 * @return String representation
 */
const char* log_manager_level_to_string(esp_log_level_t level);

#endif // LOG_MANAGER_H
