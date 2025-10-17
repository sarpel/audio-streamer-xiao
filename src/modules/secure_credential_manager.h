/**
 * @file secure_credential_manager.h
 * @brief Secure credential management system for Audio Streamer XIAO
 * @author Security Implementation
 * @date 2025
 *
 * This module provides secure storage and management of sensitive credentials
 * using ESP32's encrypted NVS (Non-Volatile Storage) and hardware security features.
 */

#ifndef SECURE_CREDENTIAL_MANAGER_H
#define SECURE_CREDENTIAL_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Maximum credential lengths for validation
#define MAX_WIFI_SSID_LENGTH 32
#define MAX_WIFI_PASSWORD_LENGTH 64
#define MAX_WEB_USERNAME_LENGTH 32
#define MAX_WEB_PASSWORD_LENGTH 64
#define MAX_DEVICE_NAME_LENGTH 32

// Credential validation patterns
#define WIFI_SSID_VALID_PATTERN "^[a-zA-Z0-9_\\-]{1,32}$"
#define WIFI_PASSWORD_MIN_LENGTH 8
#define WEB_PASSWORD_MIN_LENGTH 8
#define WEB_USERNAME_VALID_PATTERN "^[a-zA-Z0-9_]{3,32}$"

/**
 * @brief Credential types for secure storage
 */
typedef enum {
    CREDENTIAL_TYPE_WIFI_SSID,
    CREDENTIAL_TYPE_WIFI_PASSWORD,
    CREDENTIAL_TYPE_WEB_USERNAME,
    CREDENTIAL_TYPE_WEB_PASSWORD,
    CREDENTIAL_TYPE_DEVICE_NAME,
    CREDENTIAL_TYPE_MAX
} credential_type_t;

/**
 * @brief Credential validation result
 */
typedef enum {
    CREDENTIAL_VALIDATION_OK,
    CREDENTIAL_VALIDATION_TOO_SHORT,
    CREDENTIAL_VALIDATION_TOO_LONG,
    CREDENTIAL_VALIDATION_INVALID_CHARS,
    CREDENTIAL_VALIDATION_WEAK_PASSWORD,
    CREDENTIAL_VALIDATION_UNKNOWN_TYPE
} credential_validation_result_t;

/**
 * @brief Initialize secure credential manager
 * @return true if initialization successful, false otherwise
 */
bool secure_credential_manager_init(void);

/**
 * @brief Store credential securely in encrypted NVS
 * @param type Credential type to store
 * @param credential Null-terminated credential string
 * @param length Length of credential (excluding null terminator)
 * @return true if storage successful, false otherwise
 */
bool secure_credential_store(credential_type_t type, const char* credential, size_t length);

/**
 * @brief Retrieve credential from secure storage
 * @param type Credential type to retrieve
 * @param buffer Output buffer for credential
 * @param buffer_size Size of output buffer
 * @param out_length Output parameter for actual credential length
 * @return true if retrieval successful, false otherwise
 */
bool secure_credential_retrieve(credential_type_t type, char* buffer, size_t buffer_size, size_t* out_length);

/**
 * @brief Validate credential before storage
 * @param type Credential type to validate
 * @param credential Credential string to validate
 * @param length Length of credential
 * @return Validation result code
 */
credential_validation_result_t secure_credential_validate(credential_type_t type, const char* credential, size_t length);

/**
 * @brief Check if credentials are configured (not using defaults)
 * @return true if credentials are configured, false if using defaults
 */
bool secure_credential_are_configured(void);

/**
 * @brief Generate secure random password
 * @param buffer Output buffer for generated password
 * @param length Desired password length (min 12 recommended)
 * @return true if generation successful, false otherwise
 */
bool secure_credential_generate_password(char* buffer, size_t length);

/**
 * @brief Erase all credentials from secure storage
 * @return true if erase successful, false otherwise
 */
bool secure_credential_erase_all(void);

/**
 * @brief Deinitialize secure credential manager
 */
void secure_credential_manager_deinit(void);

/**
 * @brief Get credential strength score (0-100)
 * @param credential Password to evaluate
 * @param length Length of password
 * @return Strength score (higher is better)
 */
uint8_t secure_credential_get_strength_score(const char* credential, size_t length);

/**
 * @brief Check if credential has been compromised (using HaveIBeenPwned API pattern)
 * @param credential Credential to check
 * @param length Length of credential
 * @return true if credential appears compromised, false otherwise
 */
bool secure_credential_is_compromised(const char* credential, size_t length);

#ifdef __cplusplus
}
#endif

#endif // SECURE_CREDENTIAL_MANAGER_H