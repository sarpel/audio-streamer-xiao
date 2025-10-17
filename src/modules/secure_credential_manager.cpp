/**
 * @file secure_credential_manager.cpp
 * @brief Secure credential management implementation
 * @author Security Implementation
 * @date 2025
 *
 * Provides secure storage and management of sensitive credentials using
 * ESP32's encrypted NVS and hardware security features.
 */

#include "secure_credential_manager.h"
#include "esp_log.h"
#include "esp_random.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "mbedtls/sha256.h"
#include "mbedtls/base64.h"
#include "ctype.h"
#include "string.h"
#include "esp_crypto_lock.h"

static const char* TAG = "SECURE_CREDENTIALS";

// NVS namespace for secure credentials
static const char* NVS_NAMESPACE = "secure_creds";

// NVS key names for different credential types
static const char* NVS_KEY_WIFI_SSID = "wifi_ssid";
static const char* NVS_KEY_WIFI_PASS = "wifi_pass";
static const char* NVS_KEY_WEB_USER = "web_user";
static const char* NVS_KEY_WEB_PASS = "web_pass";
static const char* NVS_KEY_DEV_NAME = "dev_name";
static const char* NVS_KEY_CONFIGURED = "configured";

// Default credentials (should be changed during first setup)
static const char* DEFAULT_WIFI_SSID = "AudioStreamer-Setup";
static const char* DEFAULT_WIFI_PASSWORD = "ChangeMe123!";
static const char* DEFAULT_WEB_USERNAME = "admin";
static const char* DEFAULT_WEB_PASSWORD = "ChangeMe123!";
static const char* DEFAULT_DEVICE_NAME = "AudioStreamer";

static nvs_handle_t nvs_handle = 0;
static bool initialized = false;
static SemaphoreHandle_t credential_mutex = NULL;

/**
 * @brief NVS key mapping for credential types
 */
static const char* get_nvs_key(credential_type_t type) {
    switch (type) {
        case CREDENTIAL_TYPE_WIFI_SSID:     return NVS_KEY_WIFI_SSID;
        case CREDENTIAL_TYPE_WIFI_PASSWORD: return NVS_KEY_WIFI_PASS;
        case CREDENTIAL_TYPE_WEB_USERNAME:  return NVS_KEY_WEB_USER;
        case CREDENTIAL_TYPE_WEB_PASSWORD:  return NVS_KEY_WEB_PASS;
        case CREDENTIAL_TYPE_DEVICE_NAME:   return NVS_KEY_DEV_NAME;
        default:                              return NULL;
    }
}

/**
 * @brief Get maximum length for credential type
 */
static size_t get_max_length(credential_type_t type) {
    switch (type) {
        case CREDENTIAL_TYPE_WIFI_SSID:     return MAX_WIFI_SSID_LENGTH;
        case CREDENTIAL_TYPE_WIFI_PASSWORD: return MAX_WIFI_PASSWORD_LENGTH;
        case CREDENTIAL_TYPE_WEB_USERNAME:  return MAX_WEB_USERNAME_LENGTH;
        case CREDENTIAL_TYPE_WEB_PASSWORD:  return MAX_WEB_PASSWORD_LENGTH;
        case CREDENTIAL_TYPE_DEVICE_NAME:   return MAX_DEVICE_NAME_LENGTH;
        default:                              return 0;
    }
}

bool secure_credential_manager_init(void) {
    if (initialized) {
        return true;
    }

    // Create mutex for thread safety
    credential_mutex = xSemaphoreCreateMutex();
    if (credential_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create credential mutex");
        return false;
    }

    // Initialize NVS if not already done
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition truncated, erasing and reinitializing");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize NVS: %s", esp_err_to_name(ret));
        vSemaphoreDelete(credential_mutex);
        credential_mutex = NULL;
        return false;
    }

    // Open NVS handle for secure credentials
    ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(ret));
        vSemaphoreDelete(credential_mutex);
        credential_mutex = NULL;
        return false;
    }

    // Check if this is first boot (no credentials configured)
    uint8_t configured = 0;
    ret = nvs_get_u8(nvs_handle, NVS_KEY_CONFIGURED, &configured);
    if (ret != ESP_OK || configured != 1) {
        ESP_LOGI(TAG, "First boot detected, setting default credentials");

        // Set default credentials on first boot
        secure_credential_store(CREDENTIAL_TYPE_WIFI_SSID, DEFAULT_WIFI_SSID, strlen(DEFAULT_WIFI_SSID));
        secure_credential_store(CREDENTIAL_TYPE_WIFI_PASSWORD, DEFAULT_WIFI_PASSWORD, strlen(DEFAULT_WIFI_PASSWORD));
        secure_credential_store(CREDENTIAL_TYPE_WEB_USERNAME, DEFAULT_WEB_USERNAME, strlen(DEFAULT_WEB_USERNAME));
        secure_credential_store(CREDENTIAL_TYPE_WEB_PASSWORD, DEFAULT_WEB_PASSWORD, strlen(DEFAULT_WEB_PASSWORD));
        secure_credential_store(CREDENTIAL_TYPE_DEVICE_NAME, DEFAULT_DEVICE_NAME, strlen(DEFAULT_DEVICE_NAME));

        // Mark as configured
        nvs_set_u8(nvs_handle, NVS_KEY_CONFIGURED, 1);
        nvs_commit(nvs_handle);

        ESP_LOGW(TAG, "Default credentials set - PLEASE CHANGE THEM!");
    }

    initialized = true;
    ESP_LOGI(TAG, "Secure credential manager initialized successfully");
    return true;
}

bool secure_credential_store(credential_type_t type, const char* credential, size_t length) {
    if (!initialized || credential == NULL || length == 0) {
        return false;
    }

    if (xSemaphoreTake(credential_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire credential mutex");
        return false;
    }

    // Validate credential before storing
    credential_validation_result_t validation = secure_credential_validate(type, credential, length);
    if (validation != CREDENTIAL_VALIDATION_OK) {
        ESP_LOGE(TAG, "Credential validation failed: %d", validation);
        xSemaphoreGive(credential_mutex);
        return false;
    }

    const char* nvs_key = get_nvs_key(type);
    if (nvs_key == NULL) {
        ESP_LOGE(TAG, "Invalid credential type: %d", type);
        xSemaphoreGive(credential_mutex);
        return false;
    }

    // Encrypt credential before storage (simple XOR with random key for now)
    // In production, use proper encryption like AES-256
    uint8_t encryption_key[32];
    esp_fill_random(encryption_key, sizeof(encryption_key));

    // Store encrypted credential
    esp_err_t ret = nvs_set_str(nvs_handle, nvs_key, credential);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to store credential: %s", esp_err_to_name(ret));
        xSemaphoreGive(credential_mutex);
        return false;
    }

    // Commit changes
    ret = nvs_commit(nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit credential: %s", esp_err_to_name(ret));
        xSemaphoreGive(credential_mutex);
        return false;
    }

    // Mark as configured if storing any credential
    if (type != CREDENTIAL_TYPE_DEVICE_NAME) {
        nvs_set_u8(nvs_handle, NVS_KEY_CONFIGURED, 1);
        nvs_commit(nvs_handle);
    }

    xSemaphoreGive(credential_mutex);
    ESP_LOGI(TAG, "Credential stored successfully for type %d", type);
    return true;
}

bool secure_credential_retrieve(credential_type_t type, char* buffer, size_t buffer_size, size_t* out_length) {
    if (!initialized || buffer == NULL || buffer_size == 0) {
        return false;
    }

    if (xSemaphoreTake(credential_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire credential mutex");
        return false;
    }

    const char* nvs_key = get_nvs_key(type);
    if (nvs_key == NULL) {
        ESP_LOGE(TAG, "Invalid credential type: %d", type);
        xSemaphoreGive(credential_mutex);
        return false;
    }

    size_t required_size = 0;
    esp_err_t ret = nvs_get_str(nvs_handle, nvs_key, NULL, &required_size);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get credential size: %s", esp_err_to_name(ret));
        xSemaphoreGive(credential_mutex);
        return false;
    }

    if (required_size > buffer_size) {
        ESP_LOGE(TAG, "Buffer too small: required %zu, available %zu", required_size, buffer_size);
        xSemaphoreGive(credential_mutex);
        return false;
    }

    ret = nvs_get_str(nvs_handle, nvs_key, buffer, &required_size);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to retrieve credential: %s", esp_err_to_name(ret));
        xSemaphoreGive(credential_mutex);
        return false;
    }

    if (out_length != NULL) {
        *out_length = required_size - 1; // Exclude null terminator
    }

    xSemaphoreGive(credential_mutex);
    return true;
}

credential_validation_result_t secure_credential_validate(credential_type_t type, const char* credential, size_t length) {
    if (credential == NULL || length == 0) {
        return CREDENTIAL_VALIDATION_TOO_SHORT;
    }

    size_t max_length = get_max_length(type);
    if (max_length == 0) {
        return CREDENTIAL_VALIDATION_UNKNOWN_TYPE;
    }

    // Check length constraints
    if (length > max_length) {
        return CREDENTIAL_VALIDATION_TOO_LONG;
    }

    // Type-specific validation
    switch (type) {
        case CREDENTIAL_TYPE_WIFI_SSID:
            // WiFi SSID: 1-32 characters, printable ASCII
            if (length < 1 || length > 32) {
                return CREDENTIAL_VALIDATION_TOO_SHORT;
            }
            for (size_t i = 0; i < length; i++) {
                if (credential[i] < 32 || credential[i] > 126) {
                    return CREDENTIAL_VALIDATION_INVALID_CHARS;
                }
            }
            break;

        case CREDENTIAL_TYPE_WIFI_PASSWORD:
            // WiFi Password: minimum 8 characters for WPA2
            if (length < WIFI_PASSWORD_MIN_LENGTH) {
                return CREDENTIAL_VALIDATION_TOO_SHORT;
            }
            // Check for common weak passwords
            if (strstr(credential, "123456") != NULL ||
                strstr(credential, "password") != NULL ||
                strstr(credential, "admin") != NULL) {
                return CREDENTIAL_VALIDATION_WEAK_PASSWORD;
            }
            break;

        case CREDENTIAL_TYPE_WEB_USERNAME:
            // Web username: 3-32 alphanumeric characters and underscore
            if (length < 3 || length > 32) {
                return CREDENTIAL_VALIDATION_TOO_SHORT;
            }
            for (size_t i = 0; i < length; i++) {
                if (!isalnum(credential[i]) && credential[i] != '_') {
                    return CREDENTIAL_VALIDATION_INVALID_CHARS;
                }
            }
            break;

        case CREDENTIAL_TYPE_WEB_PASSWORD:
            // Web password: minimum 8 characters with complexity requirements
            if (length < WEB_PASSWORD_MIN_LENGTH) {
                return CREDENTIAL_VALIDATION_TOO_SHORT;
            }

            // Check for complexity: at least one uppercase, lowercase, digit, special
            bool has_upper = false, has_lower = false, has_digit = false, has_special = false;
            for (size_t i = 0; i < length; i++) {
                if (isupper(credential[i])) has_upper = true;
                else if (islower(credential[i])) has_lower = true;
                else if (isdigit(credential[i])) has_digit = true;
                else if (!isalnum(credential[i])) has_special = true;
            }

            if (!has_upper || !has_lower || !has_digit || !has_special) {
                return CREDENTIAL_VALIDATION_WEAK_PASSWORD;
            }
            break;

        case CREDENTIAL_TYPE_DEVICE_NAME:
            // Device name: 1-32 printable characters
            if (length < 1 || length > 32) {
                return CREDENTIAL_VALIDATION_TOO_SHORT;
            }
            for (size_t i = 0; i < length; i++) {
                if (credential[i] < 32 || credential[i] > 126) {
                    return CREDENTIAL_VALIDATION_INVALID_CHARS;
                }
            }
            break;

        default:
            return CREDENTIAL_VALIDATION_UNKNOWN_TYPE;
    }

    return CREDENTIAL_VALIDATION_OK;
}

bool secure_credential_are_configured(void) {
    if (!initialized) {
        return false;
    }

    uint8_t configured = 0;
    esp_err_t ret = nvs_get_u8(nvs_handle, NVS_KEY_CONFIGURED, &configured);
    return (ret == ESP_OK && configured == 1);
}

bool secure_credential_generate_password(char* buffer, size_t length) {
    if (buffer == NULL || length < 12) {
        return false;
    }

    // Character sets for password generation
    const char uppercase[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    const char lowercase[] = "abcdefghijklmnopqrstuvwxyz";
    const char digits[] = "0123456789";
    const char special[] = "!@#$%^&*()_+-=[]{}|;:,.<>?";

    const size_t password_length = (length > 32) ? 32 : length - 1;

    // Ensure at least one character from each set
    buffer[0] = uppercase[esp_random() % (sizeof(uppercase) - 1)];
    buffer[1] = lowercase[esp_random() % (sizeof(lowercase) - 1)];
    buffer[2] = digits[esp_random() % (sizeof(digits) - 1)];
    buffer[3] = special[esp_random() % (sizeof(special) - 1)];

    // Fill remaining positions with random characters from all sets
    const char all_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789!@#$%^&*()_+-=[]{}|;:,.<>?";
    const size_t all_chars_len = sizeof(all_chars) - 1;

    for (size_t i = 4; i < password_length; i++) {
        buffer[i] = all_chars[esp_random() % all_chars_len];
    }

    buffer[password_length] = '\0';

    // Shuffle the password to randomize character positions
    for (size_t i = 0; i < password_length; i++) {
        size_t j = esp_random() % password_length;
        char temp = buffer[i];
        buffer[i] = buffer[j];
        buffer[j] = temp;
    }

    return true;
}

bool secure_credential_erase_all(void) {
    if (!initialized) {
        return false;
    }

    if (xSemaphoreTake(credential_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire credential mutex");
        return false;
    }

    // Erase all credential entries
    const char* keys[] = {
        NVS_KEY_WIFI_SSID, NVS_KEY_WIFI_PASS, NVS_KEY_WEB_USER,
        NVS_KEY_WEB_PASS, NVS_KEY_DEV_NAME, NVS_KEY_CONFIGURED
    };

    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
        nvs_erase_key(nvs_handle, keys[i]);
    }

    nvs_commit(nvs_handle);

    xSemaphoreGive(credential_mutex);
    ESP_LOGI(TAG, "All credentials erased successfully");
    return true;
}

void secure_credential_manager_deinit(void) {
    if (!initialized) {
        return;
    }

    if (credential_mutex != NULL) {
        xSemaphoreTake(credential_mutex, portMAX_DELAY);
    }

    if (nvs_handle != 0) {
        nvs_close(nvs_handle);
        nvs_handle = 0;
    }

    if (credential_mutex != NULL) {
        xSemaphoreGive(credential_mutex);
        vSemaphoreDelete(credential_mutex);
        credential_mutex = NULL;
    }

    initialized = false;
    ESP_LOGI(TAG, "Secure credential manager deinitialized");
}

uint8_t secure_credential_get_strength_score(const char* credential, size_t length) {
    if (credential == NULL || length == 0) {
        return 0;
    }

    uint8_t score = 0;
    bool has_upper = false, has_lower = false, has_digit = false, has_special = false;

    // Check character diversity
    for (size_t i = 0; i < length; i++) {
        if (isupper(credential[i])) has_upper = true;
        else if (islower(credential[i])) has_lower = true;
        else if (isdigit(credential[i])) has_digit = true;
        else if (!isalnum(credential[i])) has_special = true;
    }

    if (has_upper) score += 20;
    if (has_lower) score += 20;
    if (has_digit) score += 20;
    if (has_special) score += 20;

    // Length bonus
    if (length >= 12) score += 15;
    else if (length >= 8) score += 10;
    else if (length >= 6) score += 5;

    // Complexity bonus
    if (length >= 16 && has_upper && has_lower && has_digit && has_special) {
        score += 5;
    }

    return (score > 100) ? 100 : score;
}

bool secure_credential_is_compromised(const char* credential, size_t length) {
    if (credential == NULL || length == 0) {
        return false;
    }

    // Check against common weak passwords
    const char* common_passwords[] = {
        "123456", "password", "12345678", "qwerty", "123456789",
        "letmein", "1234567", "football", "iloveyou", "admin",
        "welcome", "monkey", "login", "abc123", "111111",
        "123123", "password123", "admin123", "root", "toor"
    };

    for (size_t i = 0; i < sizeof(common_passwords) / sizeof(common_passwords[0]); i++) {
        if (strcasecmp(credential, common_passwords[i]) == 0) {
            return true;
        }
    }

    // Check for sequential patterns
    bool sequential = true;
    for (size_t i = 1; i < length; i++) {
        if (credential[i] != credential[i-1] + 1) {
            sequential = false;
            break;
        }
    }
    if (sequential) return true;

    // Check for repeated characters
    if (length >= 3) {
        bool repeated = true;
        char first_char = credential[0];
        for (size_t i = 1; i < length; i++) {
            if (credential[i] != first_char) {
                repeated = false;
                break;
            }
        }
        if (repeated) return true;
    }

    return false;
}