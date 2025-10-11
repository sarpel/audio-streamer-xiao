#include "error_handler.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char* TAG = "ERROR_HANDLER";

// Error counters
static uint32_t error_counters[10] = {0};
static bool auto_restart_enabled = true;
static nvs_handle_t nvs_handle = 0;

// Error type names for logging
static const char* error_names[] = {
    "OK",
    "INIT_FAILED",
    "NO_MEMORY",
    "NETWORK_FAILED",
    "INVALID_CONFIG",
    "I2S_FAILURE",
    "TCP_FAILURE",
    "BUFFER_OVERFLOW",
    "TIMEOUT"
};

// Severity names for logging
static const char* severity_names[] = {
    "INFO",
    "WARNING",
    "ERROR",
    "CRITICAL",
    "FATAL"
};

bool error_handler_init(void)
{
    // Open NVS for error logging
    esp_err_t err = nvs_open("error_log", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "Failed to open NVS for error logging: %s", esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG, "Error handler initialized");
    return true;
}

void system_error(system_error_t err, error_severity_t severity,
                  const char* module, const char* message)
{
    // Increment error counter
    if (err < 10)
    {
        error_counters[err]++;
    }

    // Log error with appropriate level
    const char* err_name = (err < 9) ? error_names[err] : "UNKNOWN";
    const char* sev_name = (severity < 5) ? severity_names[severity] : "UNKNOWN";

    switch (severity)
    {
        case ERR_SEVERITY_INFO:
            ESP_LOGI(TAG, "[%s] %s: %s (count: %lu)", module, err_name, message, error_counters[err]);
            break;

        case ERR_SEVERITY_WARNING:
            ESP_LOGW(TAG, "[%s] %s: %s (count: %lu)", module, err_name, message, error_counters[err]);
            break;

        case ERR_SEVERITY_ERROR:
            ESP_LOGE(TAG, "[%s] %s: %s (count: %lu)", module, err_name, message, error_counters[err]);
            break;

        case ERR_SEVERITY_CRITICAL:
        case ERR_SEVERITY_FATAL:
            ESP_LOGE(TAG, "[%s] %s - CRITICAL: %s (count: %lu)",
                     module, err_name, message, error_counters[err]);

            // Save error to NVS for post-mortem analysis
            if (nvs_handle != 0)
            {
                char key[16];
                snprintf(key, sizeof(key), "err_%d", (int)err);
                nvs_set_u32(nvs_handle, key, error_counters[err]);
                nvs_commit(nvs_handle);
            }

            // Trigger restart for fatal errors
            if (severity == ERR_SEVERITY_FATAL && auto_restart_enabled)
            {
                system_fatal_error(err, module, message);
            }
            break;
    }
}

void system_fatal_error(system_error_t err, const char* module, const char* message)
{
    const char* err_name = (err < 9) ? error_names[err] : "UNKNOWN";

    ESP_LOGE(TAG, "========================================");
    ESP_LOGE(TAG, "FATAL ERROR: %s - %s", module, err_name);
    ESP_LOGE(TAG, "Message: %s", message);
    ESP_LOGE(TAG, "Error count: %lu", error_counters[err]);
    ESP_LOGE(TAG, "========================================");

    // Save fatal error details to NVS
    if (nvs_handle != 0)
    {
        nvs_set_u32(nvs_handle, "last_fatal_err", (uint32_t)err);
        nvs_set_u32(nvs_handle, "fatal_count", error_counters[err]);
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
    }

    ESP_LOGE(TAG, "System will restart in 3 seconds...");
    vTaskDelay(pdMS_TO_TICKS(3000));

    esp_restart();
}

uint32_t error_handler_get_count(system_error_t err)
{
    if (err < 10)
    {
        return error_counters[err];
    }
    return 0;
}

void error_handler_reset_counters(void)
{
    memset(error_counters, 0, sizeof(error_counters));
    ESP_LOGI(TAG, "Error counters reset");
}

void error_handler_set_auto_restart(bool enable)
{
    auto_restart_enabled = enable;
    ESP_LOGI(TAG, "Auto-restart %s", enable ? "enabled" : "disabled");
}
