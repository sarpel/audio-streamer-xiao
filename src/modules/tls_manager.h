/**
 * @file tls_manager.h
 * @brief TLS/SSL encryption manager for secure network communications
 * @author Security Implementation
 * @date 2025
 *
 * Provides TLS/SSL encryption support for secure audio streaming with
 * certificate management, secure socket creation, and encryption protocols.
 */

#ifndef TLS_MANAGER_H
#define TLS_MANAGER_H

#include "stdint.h"
#include "stdbool.h"
#include "stddef.h"
#include "esp_tls.h"

#ifdef __cplusplus
extern "C" {
#endif

// TLS Configuration Parameters
#define TLS_MAX_CERT_SIZE 4096               // Maximum certificate size
#define TLS_MAX_KEY_SIZE 4096                // Maximum private key size
#define TLS_MAX_CA_SIZE 8192                 // Maximum CA certificate size
#define TLS_DEFAULT_TIMEOUT_MS 10000         // Default TLS handshake timeout
#define TLS_MAX_HOSTNAME_LENGTH 256          // Maximum hostname length
#define TLS_SESSION_CACHE_SIZE 8             // TLS session cache size

// Security Protocols
#define TLS_MIN_VERSION ESP_TLS_VER_TLS_1_2  // Minimum TLS version
#define TLS_MAX_VERSION ESP_TLS_VER_MAX      // Maximum TLS version
#define TLS_CIPHER_SUITE "ECDHE-RSA-AES256-GCM-SHA384:ECDHE-RSA-AES128-GCM-SHA256"

/**
 * @brief TLS certificate types
 */
typedef enum {
    TLS_CERT_TYPE_SERVER,
    TLS_CERT_TYPE_CLIENT,
    TLS_CERT_TYPE_CA,
    TLS_CERT_TYPE_SELF_SIGNED
} tls_cert_type_t;

/**
 * @brief TLS configuration structure
 */
typedef struct {
    char server_hostname[TLS_MAX_HOSTNAME_LENGTH];  // Server hostname for verification
    uint16_t server_port;                           // Server port
    tls_cert_type_t cert_type;                      // Certificate type
    bool verify_server_cert;                        // Verify server certificate
    bool verify_hostname;                           // Verify hostname matches certificate
    bool enable_session_cache;                      // Enable TLS session caching
    uint32_t handshake_timeout_ms;                  // TLS handshake timeout
    esp_tls_ver_t min_tls_version;                  // Minimum TLS version
    esp_tls_ver_t max_tls_version;                  // Maximum TLS version
    const char* cipher_suites;                      // Preferred cipher suites
} tls_config_t;

/**
 * @brief TLS certificate information
 */
typedef struct {
    char subject[256];              // Certificate subject
    char issuer[256];               // Certificate issuer
    char serial_number[64];         // Certificate serial number
    uint32_t valid_from;            // Valid from timestamp
    uint32_t valid_until;           // Valid until timestamp
    uint16_t key_bits;              // Key size in bits
    tls_cert_type_t cert_type;      // Certificate type
} tls_cert_info_t;

/**
 * @brief TLS connection statistics
 */
typedef struct {
    uint64_t bytes_encrypted;       // Total bytes encrypted
    uint64_t bytes_decrypted;       // Total bytes decrypted
    uint32_t handshake_count;       // Number of TLS handshakes
    uint32_t session_reuse_count;   // TLS session reuse count
    uint32_t error_count;           // TLS error count
    uint32_t cert_verify_failures;  // Certificate verification failures
    TickType_t last_handshake_tick; // Last handshake timestamp
} tls_stats_t;

/**
 * @brief TLS security event
 */
typedef enum {
    TLS_EVENT_HANDSHAKE_START,
    TLS_EVENT_HANDSHAKE_COMPLETE,
    TLS_EVENT_HANDSHAKE_FAILED,
    TLS_EVENT_CERT_VERIFY_FAILED,
    TLS_EVENT_SESSION_REUSED,
    TLS_EVENT_ERROR_OCCURRED,
    TLS_EVENT_CERT_EXPIRING,
    TLS_EVENT_CERT_EXPIRED
} tls_event_t;

/**
 * @brief TLS event callback
 * @param event TLS event type
 * @param data Event-specific data
 * @param user_data User-provided callback data
 */
typedef void (*tls_event_callback_t)(tls_event_t event, void* data, void* user_data);

/**
 * @brief Initialize TLS manager
 * @return true if initialization successful, false otherwise
 */
bool tls_manager_init(void);

/**
 * @brief Create TLS configuration with defaults
 * @param config Output configuration structure
 * @param server_hostname Server hostname for connection
 * @param server_port Server port number
 * @return true if configuration created, false otherwise
 */
bool tls_manager_create_config(tls_config_t* config, const char* server_hostname, uint16_t server_port);

/**
 * @brief Load certificate from memory
 * @param cert_data Certificate data
 * @param cert_size Certificate data size
 * @param cert_type Certificate type
 * @return true if certificate loaded, false otherwise
 */
bool tls_manager_load_certificate(const uint8_t* cert_data, size_t cert_size, tls_cert_type_t cert_type);

/**
 * @brief Load certificate from file
 * @param cert_path Certificate file path
 * @param cert_type Certificate type
 * @return true if certificate loaded, false otherwise
 */
bool tls_manager_load_certificate_from_file(const char* cert_path, tls_cert_type_t cert_type);

/**
 * @brief Generate self-signed certificate
 * @param cert_info Certificate information
 * @param validity_days Certificate validity in days
 * @return true if certificate generated, false otherwise
 */
bool tls_manager_generate_self_signed_cert(const tls_cert_info_t* cert_info, uint32_t validity_days);

/**
 * @brief Create TLS connection
 * @param config TLS configuration
 * @return TLS connection handle or NULL on error
 */
esp_tls_t* tls_manager_create_connection(const tls_config_t* config);

/**
 * @brief Perform TLS handshake
 * @param tls TLS connection handle
 * @param config TLS configuration
 * @return true if handshake successful, false otherwise
 */
bool tls_manager_perform_handshake(esp_tls_t* tls, const tls_config_t* config);

/**
 * @brief Send data over TLS connection
 * @param tls TLS connection handle
 * @param data Data to send
 * @param data_len Data length
 * @return Number of bytes sent or negative on error
 */
ssize_t tls_manager_send(esp_tls_t* tls, const void* data, size_t data_len);

/**
 * @brief Receive data from TLS connection
 * @param tls TLS connection handle
 * @param buffer Buffer to receive data
 * @param buffer_len Buffer length
 * @return Number of bytes received or negative on error
 */
ssize_t tls_manager_receive(esp_tls_t* tls, void* buffer, size_t buffer_len);

/**
 * @brief Close TLS connection
 * @param tls TLS connection handle
 */
void tls_manager_close_connection(esp_tls_t* tls);

/**
 * @brief Get TLS connection statistics
 * @param tls TLS connection handle
 * @param stats Statistics structure to fill
 * @return true if statistics retrieved, false otherwise
 */
bool tls_manager_get_stats(esp_tls_t* tls, tls_stats_t* stats);

/**
 * @brief Get certificate information
 * @param tls TLS connection handle
 * @param cert_info Certificate info structure to fill
 * @return true if certificate info retrieved, false otherwise
 */
bool tls_manager_get_cert_info(esp_tls_t* tls, tls_cert_info_t* cert_info);

/**
 * @brief Validate certificate
 * @param cert_data Certificate data
 * @param cert_size Certificate size
 * @param cert_type Certificate type
 * @return true if certificate valid, false otherwise
 */
bool tls_manager_validate_certificate(const uint8_t* cert_data, size_t cert_size, tls_cert_type_t cert_type);

/**
 * @brief Check if certificate is expiring
 * @param cert_info Certificate information
 * @param days_before Number of days before expiration to warn
 * @return true if certificate expiring soon, false otherwise
 */
bool tls_manager_is_cert_expiring(const tls_cert_info_t* cert_info, uint32_t days_before);

/**
 * @brief Set TLS event callback
 * @param callback Event callback function
 * @param user_data User data for callback
 * @return true if callback set, false otherwise
 */
bool tls_manager_set_event_callback(tls_event_callback_t callback, void* user_data);

/**
 * @brief Perform TLS certificate pinning
 * @param tls TLS connection handle
 * @param expected_cert Expected certificate fingerprint
 * @param fingerprint_size Fingerprint size
 * @return true if certificate matches, false otherwise
 */
bool tls_manager_pin_certificate(esp_tls_t* tls, const uint8_t* expected_cert, size_t fingerprint_size);

/**
 * @brief Enable TLS session caching
 * @param enable true to enable, false to disable
 * @return true if operation successful, false otherwise
 */
bool tls_manager_enable_session_cache(bool enable);

/**
 * @brief Clear TLS session cache
 * @return true if cache cleared, false otherwise
 */
bool tls_manager_clear_session_cache(void);

/**
 * @brief Get TLS error string
 * @param error_code TLS error code
 * @return Human-readable error string
 */
const char* tls_manager_get_error_string(esp_tls_error_t error_code);

/**
 * @brief Perform security audit of TLS configuration
 * @param config TLS configuration to audit
 * @param audit_results Buffer for audit results
 * @param results_size Size of results buffer
 * @return true if audit completed, false otherwise
 */
bool tls_manager_security_audit(const tls_config_t* config, char* audit_results, size_t results_size);

/**
 * @brief Create secure TLS server
 * @param config TLS configuration for server
 * @return TLS server handle or NULL on error
 */
esp_tls_t* tls_manager_create_server(const tls_config_t* config);

/**
 * @brief Accept TLS client connection
 * @param server_tls Server TLS handle
 * @param client_fd Client file descriptor
 * @return Client TLS handle or NULL on error
 */
esp_tls_t* tls_manager_accept_client(esp_tls_t* server_tls, int client_fd);

/**
 * @brief Deinitialize TLS manager
 */
void tls_manager_deinit(void);

/**
 * @brief Secure TLS wrapper for audio streaming
 */
typedef struct {
    esp_tls_t* tls;
    tls_config_t config;
    tls_stats_t stats;
    bool is_server;  // true for server, false for client
} secure_tls_connection_t;

/**
 * @brief Create secure TLS connection for audio streaming
 * @param connection Secure connection structure
 * @param is_server true for server mode, false for client mode
 * @return true if connection created, false otherwise
 */
bool tls_manager_create_secure_connection(secure_tls_connection_t* connection, bool is_server);

/**
 * @brief Stream encrypted audio data
 * @param connection Secure connection
 * @param audio_data Audio data to stream
 * @param data_size Size of audio data
 * @param encrypted_buffer Buffer for encrypted data
 * @param buffer_size Size of encrypted buffer
 * @return Number of encrypted bytes or negative on error
 */
ssize_t tls_manager_stream_encrypted_audio(secure_tls_connection_t* connection,
                                           const int16_t* audio_data, size_t data_size,
                                           uint8_t* encrypted_buffer, size_t buffer_size);

/**
 * @brief Receive and decrypt audio data
 * @param connection Secure connection
 * @param encrypted_data Encrypted audio data
 * @param encrypted_size Size of encrypted data
 * @param audio_buffer Buffer for decrypted audio
 * @param buffer_size Size of audio buffer
 * @return Number of decrypted samples or negative on error
 */
ssize_t tls_manager_receive_decrypted_audio(secure_tls_connection_t* connection,
                                            const uint8_t* encrypted_data, size_t encrypted_size,
                                            int16_t* audio_buffer, size_t buffer_size);

/**
 * @brief Close secure TLS connection
 * @param connection Secure connection
 */
void tls_manager_close_secure_connection(secure_tls_connection_t* connection);

#ifdef __cplusplus
}
#endif

#endif // TLS_MANAGER_H

/*
 * Security Features:
 * - TLS 1.2/1.3 support with modern cipher suites
 * - Certificate validation and pinning
 * - Session caching for performance
 * - Hardware crypto acceleration support
 * - Comprehensive error handling
 * - Security event monitoring
 * - Certificate expiration warnings
 *
 * Usage Example:
 * tls_config_t config;
 * tls_manager_create_config(&config, "audio.example.com", 443);
 *
 * esp_tls_t* tls = tls_manager_create_connection(&config);
 * if (tls_manager_perform_handshake(tls, &config)) {
 *     // Send encrypted audio data
 *     tls_manager_send(tls, audio_data, data_size);
 * }
 *
 * tls_manager_close_connection(tls);
 */