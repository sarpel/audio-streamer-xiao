#include "log_manager.h"
#include "esp_timer.h"
#include <string.h>
#include <stdio.h>

static const char* TAG = "LOG_MANAGER";

// Circular buffer for log storage
static log_entry_t log_buffer[MAX_LOG_ENTRIES];
static size_t log_write_index = 0;
static size_t log_count = 0;
static bool log_buffer_full = false;

// Mutex for thread-safe access
static SemaphoreHandle_t log_mutex = NULL;

// Original vprintf function pointer
static vprintf_like_t original_vprintf = NULL;

// Custom vprintf hook to capture logs
static int log_vprintf_hook(const char* format, va_list args) {
    // Call original vprintf to maintain normal console output
    int ret = original_vprintf(format, args);

    // Parse the log message (ESP-IDF format: "LEVEL (timestamp) TAG: message")
    char buffer[256];
    vsnprintf(buffer, sizeof(buffer), format, args);

    // Try to extract log level, tag, and message from ESP-IDF format
    // Format: "E (12345) TAG: message\n"
    char level_char = buffer[0];
    esp_log_level_t level = ESP_LOG_INFO;

    switch(level_char) {
        case 'E': level = ESP_LOG_ERROR; break;
        case 'W': level = ESP_LOG_WARN; break;
        case 'I': level = ESP_LOG_INFO; break;
        case 'D': level = ESP_LOG_DEBUG; break;
        case 'V': level = ESP_LOG_VERBOSE; break;
    }

    // Find tag between parentheses and colon
    char tag[MAX_LOG_TAG_LEN] = "SYSTEM";
    char message[MAX_LOG_MESSAGE_LEN] = "";

    char* tag_start = strchr(buffer, ')');
    if (tag_start) {
        tag_start++; // Skip ')'
        while (*tag_start == ' ') tag_start++; // Skip spaces

        char* tag_end = strchr(tag_start, ':');
        if (tag_end) {
            size_t tag_len = tag_end - tag_start;
            if (tag_len > 0 && tag_len < MAX_LOG_TAG_LEN) {
                strncpy(tag, tag_start, tag_len);
                tag[tag_len] = '\0';
            }

            // Extract message
            char* msg_start = tag_end + 1;
            while (*msg_start == ' ') msg_start++; // Skip spaces

            strncpy(message, msg_start, MAX_LOG_MESSAGE_LEN - 1);
            message[MAX_LOG_MESSAGE_LEN - 1] = '\0';

            // Remove trailing newline
            size_t msg_len = strlen(message);
            if (msg_len > 0 && message[msg_len - 1] == '\n') {
                message[msg_len - 1] = '\0';
            }
        }
    }

    // Store log entry
    if (log_mutex && xSemaphoreTake(log_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        log_entry_t* entry = &log_buffer[log_write_index];

        entry->timestamp = (uint32_t)(esp_timer_get_time() / 1000); // Convert to milliseconds
        entry->level = level;
        strncpy(entry->tag, tag, MAX_LOG_TAG_LEN - 1);
        entry->tag[MAX_LOG_TAG_LEN - 1] = '\0';
        strncpy(entry->message, message, MAX_LOG_MESSAGE_LEN - 1);
        entry->message[MAX_LOG_MESSAGE_LEN - 1] = '\0';

        log_write_index = (log_write_index + 1) % MAX_LOG_ENTRIES;

        if (log_count < MAX_LOG_ENTRIES) {
            log_count++;
        } else {
            log_buffer_full = true;
        }

        xSemaphoreGive(log_mutex);
    }

    return ret;
}

bool log_manager_init(void) {
    // Create mutex
    log_mutex = xSemaphoreCreateMutex();
    if (log_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create log mutex");
        return false;
    }

    // Hook into ESP-IDF logging system
    original_vprintf = esp_log_set_vprintf(log_vprintf_hook);

    ESP_LOGI(TAG, "Log manager initialized (buffer size: %d entries)", MAX_LOG_ENTRIES);

    return true;
}

size_t log_manager_get_logs(log_entry_t* logs, size_t max_count) {
    if (logs == NULL || max_count == 0) {
        return 0;
    }

    size_t count = 0;

    if (xSemaphoreTake(log_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        // Determine start index (oldest log)
        size_t start_index = log_buffer_full ? log_write_index : 0;
        size_t entries_to_copy = (log_count < max_count) ? log_count : max_count;

        for (size_t i = 0; i < entries_to_copy; i++) {
            size_t src_index = (start_index + i) % MAX_LOG_ENTRIES;
            memcpy(&logs[count], &log_buffer[src_index], sizeof(log_entry_t));
            count++;
        }

        xSemaphoreGive(log_mutex);
    }

    return count;
}

size_t log_manager_get_count(void) {
    return log_count;
}

void log_manager_clear(void) {
    if (xSemaphoreTake(log_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        log_write_index = 0;
        log_count = 0;
        log_buffer_full = false;
        memset(log_buffer, 0, sizeof(log_buffer));
        xSemaphoreGive(log_mutex);

        ESP_LOGI(TAG, "Logs cleared");
    }
}

const char* log_manager_level_to_string(esp_log_level_t level) {
    switch(level) {
        case ESP_LOG_ERROR:   return "ERROR";
        case ESP_LOG_WARN:    return "WARN";
        case ESP_LOG_INFO:    return "INFO";
        case ESP_LOG_DEBUG:   return "DEBUG";
        case ESP_LOG_VERBOSE: return "VERBOSE";
        default:              return "UNKNOWN";
    }
}
