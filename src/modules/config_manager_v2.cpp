#include "config_manager_v2.h"
#include "../config.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_timer.h"
#include "cJSON.h"
#include <string.h>

static const char* TAG = "CONFIG_MANAGER_V2";
static const char* NAMESPACE = "unified_config";

// Global configuration instance
static unified_config_t current_config;
static unified_config_t saved_config;
static bool config_initialized = false;
static bool has_unsaved_changes = false;

// NVS key for storing configuration
static const char* CONFIG_KEY = "unified_config";

// Available categories
static const char* AVAILABLE_CATEGORIES[] = {
    "network", "server", "audio", "buffer", "task",
    "error", "debug", "auth", "ntp", "udp", "tcp", "performance"
};

bool config_manager_v2_init(void) {
    if (config_initialized) {
        ESP_LOGW(TAG, "Configuration manager already initialized");
        return true;
    }

    // Initialize NVS
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGI(TAG, "Erasing NVS flash and initializing");
        err = nvs_flash_erase();
        if (err == ESP_OK) {
            err = nvs_flash_init();
        }
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize NVS: %s", esp_err_to_name(err));
        return false;
    }

    // Initialize configuration with defaults
    config_schema_init_defaults(&current_config);
    memcpy(&saved_config, &current_config, sizeof(unified_config_t));

    config_initialized = true;
    has_unsaved_changes = false;

    ESP_LOGI(TAG, "Configuration manager v2 initialized successfully");
    return true;
}

bool config_manager_v2_load(void) {
    if (!config_initialized) {
        ESP_LOGE(TAG, "Configuration manager not initialized");
        return false;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "No unified configuration found, checking for legacy configuration");

        // Try to migrate from legacy configuration
        unified_config_t legacy_config;
        if (config_schema_convert_legacy(NULL, &legacy_config)) {
            memcpy(&current_config, &legacy_config, sizeof(unified_config_t));
            memcpy(&saved_config, &current_config, sizeof(unified_config_t));
            has_unsaved_changes = true; // Mark as needing save to persist migration

            ESP_LOGI(TAG, "Legacy configuration migrated to unified format");
            return true;
        } else {
            ESP_LOGW(TAG, "No configuration found, using factory defaults");
            return true; // Use defaults if no saved config
        }
    }

    size_t required_size = sizeof(unified_config_t);
    err = nvs_get_blob(nvs_handle, CONFIG_KEY, &current_config, &required_size);

    if (err == ESP_OK && required_size == sizeof(unified_config_t)) {
        // Validate loaded configuration
        config_validation_result_t results[10];
        size_t issue_count = config_schema_validate_config(&current_config, results, 10);

        if (issue_count > 0) {
            ESP_LOGW(TAG, "Loaded configuration has %zu validation issues", issue_count);
            for (size_t i = 0; i < issue_count; i++) {
                ESP_LOGW(TAG, "  Field %d: %s", results[i].field_id, results[i].error_message);
            }
        }

        memcpy(&saved_config, &current_config, sizeof(unified_config_t));
        has_unsaved_changes = false;

        ESP_LOGI(TAG, "Configuration loaded successfully (version %d)", current_config.version);
        nvs_close(nvs_handle);
        return true;
    } else {
        ESP_LOGW(TAG, "Failed to load configuration: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);

        // Try legacy migration as fallback
        ESP_LOGI(TAG, "Attempting legacy configuration migration as fallback");
        unified_config_t legacy_config;
        if (config_schema_convert_legacy(NULL, &legacy_config)) {
            memcpy(&current_config, &legacy_config, sizeof(unified_config_t));
            memcpy(&saved_config, &current_config, sizeof(unified_config_t));
            has_unsaved_changes = true;

            ESP_LOGI(TAG, "Legacy configuration migrated successfully");
            return true;
        }

        return false;
    }
}

bool config_manager_v2_save(void) {
    if (!config_initialized) {
        ESP_LOGE(TAG, "Configuration manager not initialized");
        return false;
    }

    // Validate before saving
    config_validation_result_t results[10];
    size_t issue_count = config_schema_validate_config(&current_config, results, 10);

    if (issue_count > 0) {
        ESP_LOGE(TAG, "Cannot save invalid configuration (%zu issues):", issue_count);
        for (size_t i = 0; i < issue_count; i++) {
            ESP_LOGE(TAG, "  Field %d: %s", results[i].field_id, results[i].error_message);
        }
        return false;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return false;
    }

    // Update metadata
    current_config.last_updated = esp_timer_get_time() / 1000;
    current_config.version = CONFIG_SCHEMA_VERSION;

    err = nvs_set_blob(nvs_handle, CONFIG_KEY, &current_config, sizeof(unified_config_t));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save configuration: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return false;
    }

    err = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit NVS: %s", esp_err_to_name(err));
        return false;
    }

    memcpy(&saved_config, &current_config, sizeof(unified_config_t));
    has_unsaved_changes = false;

    ESP_LOGI(TAG, "Configuration saved successfully");
    return true;
}

bool config_manager_v2_reset_to_factory(void) {
    if (!config_initialized) {
        ESP_LOGE(TAG, "Configuration manager not initialized");
        return false;
    }

    ESP_LOGI(TAG, "Resetting configuration to factory defaults");
    config_schema_init_defaults(&current_config);
    has_unsaved_changes = true;

    return config_manager_v2_save();
}

bool config_manager_v2_get_config(unified_config_t* config) {
    if (!config_initialized || !config) {
        return false;
    }

    memcpy(config, &current_config, sizeof(unified_config_t));
    return true;
}

bool config_manager_v2_set_config(const unified_config_t* config) {
    if (!config_initialized || !config) {
        return false;
    }

    // Validate the new configuration
    config_validation_result_t results[10];
    size_t issue_count = config_schema_validate_config(config, results, 10);

    if (issue_count > 0) {
        ESP_LOGE(TAG, "Cannot set invalid configuration (%zu issues):", issue_count);
        for (size_t i = 0; i < issue_count; i++) {
            ESP_LOGE(TAG, "  Field %d: %s", results[i].field_id, results[i].error_message);
        }
        return false;
    }

    memcpy(&current_config, config, sizeof(unified_config_t));
    has_unsaved_changes = true;

    return true;
}

bool config_manager_v2_get_field(config_field_id_t field_id,
                                char* buffer,
                                size_t buffer_size) {
    if (!config_initialized || !buffer || buffer_size == 0) {
        return false;
    }

    return config_schema_get_field_value(&current_config, field_id, buffer, buffer_size);
}

bool config_manager_v2_set_field(config_field_id_t field_id,
                                const char* value,
                                config_validation_result_t* result) {
    if (!config_initialized || !value) {
        return false;
    }

    config_validation_result_t validation;
    config_validation_result_t* validation_ptr = result ? result : &validation;

    if (config_schema_set_field_value(&current_config, field_id, value, validation_ptr)) {
        has_unsaved_changes = true;
        return true;
    }

    return false;
}

bool config_manager_v2_get_category(const char* category,
                                    char* output,
                                    size_t buffer_size) {
    if (!config_initialized || !category || !output || buffer_size == 0) {
        return false;
    }

    cJSON* root = cJSON_CreateObject();
    if (!root) {
        return false;
    }

    const config_field_meta_t* fields[32];
    size_t field_count = config_schema_get_fields_by_category(category, fields, 32);

    for (size_t i = 0; i < field_count; i++) {
        char value[128];
        if (config_schema_get_field_value(&current_config, fields[i]->id, value, sizeof(value))) {
            cJSON_AddStringToObject(root, fields[i]->name, value);
        }
    }

    char* json_string = cJSON_Print(root);
    if (json_string) {
        strncpy(output, json_string, buffer_size - 1);
        output[buffer_size - 1] = '\0';
        free(json_string);
        cJSON_Delete(root);
        return true;
    }

    cJSON_Delete(root);
    return false;
}

size_t config_manager_v2_set_category(const char* category,
                                     const char* json_input,
                                     config_validation_result_t* results,
                                     size_t max_results) {
    if (!config_initialized || !category || !json_input || !results || max_results == 0) {
        return 0;
    }

    cJSON* root = cJSON_Parse(json_input);
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse JSON input");
        return 0;
    }

    size_t issue_count = 0;
    bool has_changes = false;

    // Iterate through all fields in the category
    const config_field_meta_t* fields[32];
    size_t field_count = config_schema_get_fields_by_category(category, fields, 32);

    for (size_t i = 0; i < field_count && issue_count < max_results; i++) {
        cJSON* item = cJSON_GetObjectItem(root, fields[i]->name);
        if (item && cJSON_IsString(item)) {
            config_validation_result_t validation;
            if (config_schema_set_field_value(&current_config, fields[i]->id, item->valuestring, &validation)) {
                has_changes = true;
            } else {
                results[issue_count++] = validation;
            }
        }
    }

    if (has_changes) {
        has_unsaved_changes = true;
    }

    cJSON_Delete(root);
    return issue_count;
}

size_t config_manager_v2_validate(config_validation_result_t* results,
                                 size_t max_results) {
    if (!config_initialized || !results || max_results == 0) {
        return 0;
    }

    return config_schema_validate_config(&current_config, results, max_results);
}

bool config_manager_v2_export_json(char* json_output, size_t buffer_size) {
    if (!config_initialized || !json_output || buffer_size == 0) {
        return false;
    }

    cJSON* root = cJSON_CreateObject();
    if (!root) {
        return false;
    }

    // Add metadata
    cJSON_AddNumberToObject(root, "version", current_config.version);
    cJSON_AddNumberToObject(root, "last_updated", current_config.last_updated);

    // Add all fields
    char value[128];
    for (int i = 0; i < CONFIG_FIELD_COUNT; i++) {
        const config_field_meta_t* meta = config_schema_get_field_meta((config_field_id_t)i);
        if (meta && config_schema_get_field_value(&current_config, meta->id, value, sizeof(value))) {
            // Skip password fields for security
            if (strcmp(meta->name, "wifi_password") == 0 || strcmp(meta->name, "auth_password") == 0) {
                cJSON_AddStringToObject(root, meta->name, "********");
            } else {
                cJSON_AddStringToObject(root, meta->name, value);
            }
        }
    }

    char* json_string = cJSON_Print(root);
    if (json_string) {
        if (strlen(json_string) < buffer_size) {
            strcpy(json_output, json_string);
            free(json_string);
            cJSON_Delete(root);
            return true;
        }
        free(json_string);
    }

    cJSON_Delete(root);
    return false;
}

size_t config_manager_v2_import_json(const char* json_input,
                                    bool overwrite,
                                    config_validation_result_t* results,
                                    size_t max_results) {
    if (!config_initialized || !json_input || !results || max_results == 0) {
        return 0;
    }

    cJSON* root = cJSON_Parse(json_input);
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse JSON input");
        return 0;
    }

    size_t issue_count = 0;
    bool has_changes = false;

    for (int i = 0; i < CONFIG_FIELD_COUNT && issue_count < max_results; i++) {
        const config_field_meta_t* meta = config_schema_get_field_meta((config_field_id_t)i);
        if (!meta) continue;

        cJSON* item = cJSON_GetObjectItem(root, meta->name);
        if (!item || !cJSON_IsString(item)) {
            continue;
        }

        // Skip password fields unless explicitly requested
        if (!overwrite && (strcmp(meta->name, "wifi_password") == 0 || strcmp(meta->name, "auth_password") == 0)) {
            continue;
        }

        config_validation_result_t validation;
        if (config_schema_set_field_value(&current_config, meta->id, item->valuestring, &validation)) {
            has_changes = true;
        } else {
            results[issue_count++] = validation;
        }
    }

    if (has_changes) {
        has_unsaved_changes = true;
    }

    cJSON_Delete(root);
    return issue_count;
}

uint8_t config_manager_v2_get_version(void) {
    if (!config_initialized) {
        return 0;
    }
    return current_config.version;
}

bool config_manager_v2_is_first_boot(void) {
    if (!config_initialized) {
        return false;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        return true; // No NVS data = first boot
    }

    size_t required_size = sizeof(unified_config_t);
    err = nvs_get_blob(nvs_handle, CONFIG_KEY, NULL, &required_size);
    nvs_close(nvs_handle);

    return (err != ESP_OK);
}

const config_field_meta_t* config_manager_v2_get_field_meta(config_field_id_t field_id) {
    return config_schema_get_field_meta(field_id);
}

size_t config_manager_v2_get_categories(char** categories, size_t max_categories) {
    if (!categories || max_categories == 0) {
        return 0;
    }

    size_t count = sizeof(AVAILABLE_CATEGORIES) / sizeof(AVAILABLE_CATEGORIES[0]);
    if (count > max_categories) {
        count = max_categories;
    }

    for (size_t i = 0; i < count; i++) {
        categories[i] = strdup(AVAILABLE_CATEGORIES[i]);
    }

    return count;
}

bool config_manager_v2_restart_required(config_field_id_t field_id) {
    const config_field_meta_t* meta = config_schema_get_field_meta(field_id);
    return meta ? meta->restart_required : false;
}

bool config_manager_v2_get_field_default(config_field_id_t field_id,
                                       char* buffer,
                                       size_t buffer_size) {
    return config_schema_get_field_default(field_id, buffer, buffer_size);
}

bool config_manager_v2_reset_field(config_field_id_t field_id) {
    if (!config_initialized) {
        return false;
    }

    char default_value[64];
    if (config_schema_get_field_default(field_id, default_value, sizeof(default_value))) {
        return config_manager_v2_set_field(field_id, default_value, NULL);
    }

    return false;
}

bool config_manager_v2_has_unsaved_changes(void) {
    return has_unsaved_changes;
}

void config_manager_v2_mark_saved(void) {
    has_unsaved_changes = false;
    memcpy(&saved_config, &current_config, sizeof(unified_config_t));
}

void config_manager_v2_deinit(void) {
    if (config_initialized) {
        if (has_unsaved_changes) {
            ESP_LOGW(TAG, "Deinitializing with unsaved changes");
        }
        config_initialized = false;
        ESP_LOGI(TAG, "Configuration manager v2 deinitialized");
    }
}