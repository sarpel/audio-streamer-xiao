/**
 * @file json_validator.h
 * @brief JSON validation and sanitization for secure configuration
 * @author Security Implementation
 * @date 2025
 *
 * Provides comprehensive JSON validation, schema enforcement, and input sanitization
 * to prevent injection attacks and buffer overflow vulnerabilities in configuration imports.
 */

#ifndef JSON_VALIDATOR_H
#define JSON_VALIDATOR_H

#include "stdint.h"
#include "stdbool.h"
#include "stddef.h"
#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

// JSON validation limits for security
#define JSON_MAX_DEPTH 10                    // Maximum JSON nesting depth
#define JSON_MAX_STRING_LENGTH 256           // Maximum string field length
#define JSON_MAX_ARRAY_ELEMENTS 100          // Maximum array elements
#define JSON_MAX_OBJECT_KEYS 50              // Maximum object keys
#define JSON_MAX_TOTAL_SIZE 16384            // Maximum total JSON size (16KB)

// Field validation patterns
#define JSON_STRING_PATTERN_ALPHANUMERIC "^[a-zA-Z0-9_\\-]+$"
#define JSON_STRING_PATTERN_IP_ADDRESS "^(?:(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\\.){3}(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$"
#define JSON_STRING_PATTERN_HOSTNAME "^[a-zA-Z0-9]([a-zA-Z0-9\\-]{0,61}[a-zA-Z0-9])?(\\.[a-zA-Z0-9]([a-zA-Z0-9\\-]{0,61}[a-zA-Z0-9])?)*$"
#define JSON_STRING_PATTERN_MAC_ADDRESS "^([0-9A-Fa-f]{2}[:-]){5}([0-9A-Fa-f]{2})$"

/**
 * @brief JSON validation result codes
 */
typedef enum {
    JSON_VALIDATION_OK,
    JSON_VALIDATION_NULL,
    JSON_VALIDATION_TOO_DEEP,
    JSON_VALIDATION_TOO_LARGE,
    JSON_VALIDATION_INVALID_TYPE,
    JSON_VALIDATION_STRING_TOO_LONG,
    JSON_VALIDATION_ARRAY_TOO_LARGE,
    JSON_VALIDATION_OBJECT_TOO_LARGE,
    JSON_VALIDATION_INVALID_PATTERN,
    JSON_VALIDATION_MISSING_REQUIRED,
    JSON_VALIDATION_OUT_OF_RANGE,
    JSON_VALIDATION_MALICIOUS_CONTENT,
    JSON_VALIDATION_SCHEMA_MISMATCH
} json_validation_result_t;

/**
 * @brief JSON field validation rule
 */
typedef struct {
    const char* field_name;          // Field name to validate
    cJSON_Type expected_type;        // Expected JSON type
    bool required;                   // Whether field is required
    size_t min_length;               // Minimum string length (for strings)
    size_t max_length;               // Maximum string length (for strings)
    int32_t min_value;               // Minimum numeric value
    int32_t max_value;               // Maximum numeric value
    const char* pattern;             // Regex pattern (for strings)
    const char** allowed_values;     // Array of allowed string values (NULL terminated)
    size_t allowed_count;            // Number of allowed values
} json_field_rule_t;

/**
 * @brief JSON schema definition
 */
typedef struct {
    const char* schema_name;         // Schema name for identification
    const json_field_rule_t* rules;  // Array of field validation rules
    size_t rule_count;               // Number of validation rules
    const char** required_fields;    // Array of required field names
    size_t required_count;           // Number of required fields
    bool allow_additional_fields;    // Whether to allow fields not in schema
} json_schema_t;

/**
 * @brief Initialize JSON validator
 * @return true if initialization successful, false otherwise
 */
bool json_validator_init(void);

/**
 * @brief Validate JSON structure (depth, size, basic security)
 * @param json JSON object to validate
 * @return Validation result code
 */
json_validation_result_t json_validator_structure(const cJSON* json);

/**
 * @brief Validate JSON against a schema
 * @param json JSON object to validate
 * @param schema Schema definition to validate against
 * @return Validation result code
 */
json_validation_result_t json_validator_schema(const cJSON* json, const json_schema_t* schema);

/**
 * @brief Validate WiFi configuration JSON
 * @param json WiFi configuration JSON object
 * @return Validation result code
 */
json_validation_result_t json_validator_wifi_config(const cJSON* json);

/**
 * @brief Validate network streaming configuration JSON
 * @param json Network configuration JSON object
 * @return Validation result code
 */
json_validation_result_t json_validator_network_config(const cJSON* json);

/**
 * @brief Validate audio configuration JSON
 * @param json Audio configuration JSON object
 * @return Validation result code
 */
json_validation_result_t json_validator_audio_config(const cJSON* json);

/**
 * @brief Validate web server configuration JSON
 * @param json Web server configuration JSON object
 * @return Validation result code
 */
json_validation_result_t json_validator_web_config(const cJSON* json);

/**
 * @brief Sanitize JSON string by removing potentially dangerous characters
 * @param input Input string to sanitize
 * @param output Output buffer for sanitized string
 * @param output_size Size of output buffer
 * @return true if sanitization successful, false otherwise
 */
bool json_validator_sanitize_string(const char* input, char* output, size_t output_size);

/**
 * @brief Check if string contains malicious patterns
 * @param str String to check
 * @return true if malicious content detected, false otherwise
 */
bool json_validator_contains_malicious(const char* str);

/**
 * @brief Validate string against regex pattern
 * @param str String to validate
 * @param pattern Regex pattern
 * @return true if string matches pattern, false otherwise
 */
bool json_validator_match_pattern(const char* str, const char* pattern);

/**
 * @brief Validate IP address string
 * @param ip IP address string to validate
 * @return true if valid IP address, false otherwise
 */
bool json_validator_is_valid_ip(const char* ip);

/**
 * @brief Validate hostname string
 * @param hostname Hostname string to validate
 * @return true if valid hostname, false otherwise
 */
bool json_validator_is_valid_hostname(const char* hostname);

/**
 * @brief Validate port number
 * @param port Port number to validate
 * @return true if valid port (1-65535), false otherwise
 */
bool json_validator_is_valid_port(int port);

/**
 * @brief Validate MAC address string
 * @param mac MAC address string to validate
 * @return true if valid MAC address, false otherwise
 */
bool json_validator_is_valid_mac(const char* mac);

/**
 * @brief Create common schemas for configuration validation
 * @param schema_type Type of schema to create
 * @param output_schema Output schema structure
 * @return true if schema created successfully, false otherwise
 */
bool json_validator_create_common_schema(const char* schema_type, json_schema_t* output_schema);

/**
 * @brief Get validation result as human-readable string
 * @param result Validation result code
 * @return Human-readable error message
 */
const char* json_validator_result_string(json_validation_result_t result);

/**
 * @brief Get validation result with detailed error information
 * @param result Validation result code
 * @param field_name Field that caused validation failure (can be NULL)
 * @param error_buffer Buffer for detailed error message
 * @param buffer_size Size of error buffer
 * @return Detailed error message
 */
const char* json_validator_detailed_error(json_validation_result_t result, const char* field_name,
                                          char* error_buffer, size_t buffer_size);

/**
 * @brief Comprehensive JSON validation with all security checks
 * @param json_string JSON string to validate
 * @param schema Optional schema to validate against (can be NULL)
 * @param validation_result Output validation result
 * @return true if JSON is valid and secure, false otherwise
 */
bool json_validator_secure_parse(const char* json_string, const json_schema_t* schema,
                                 json_validation_result_t* validation_result);

/**
 * @brief Deinitialize JSON validator
 */
void json_validator_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // JSON_VALIDATOR_H