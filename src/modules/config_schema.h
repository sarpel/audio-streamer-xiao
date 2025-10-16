#ifndef CONFIG_SCHEMA_H
#define CONFIG_SCHEMA_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

// Configuration version for migration
#define CONFIG_SCHEMA_VERSION 2

// Configuration field identifiers
typedef enum {
    // Network fields
    CONFIG_FIELD_WIFI_SSID = 0,
    CONFIG_FIELD_WIFI_PASSWORD,
    CONFIG_FIELD_WIFI_USE_STATIC_IP,
    CONFIG_FIELD_WIFI_STATIC_IP,
    CONFIG_FIELD_WIFI_GATEWAY,
    CONFIG_FIELD_WIFI_SUBNET,
    CONFIG_FIELD_WIFI_DNS_PRIMARY,
    CONFIG_FIELD_WIFI_DNS_SECONDARY,

    // Server fields
    CONFIG_FIELD_TCP_SERVER_IP,
    CONFIG_FIELD_TCP_SERVER_PORT,
    CONFIG_FIELD_UDP_SERVER_IP,
    CONFIG_FIELD_UDP_SERVER_PORT,
    CONFIG_FIELD_STREAMING_PROTOCOL,

    // Audio fields (fixed values, but included for completeness)
    CONFIG_FIELD_AUDIO_SAMPLE_RATE,
    CONFIG_FIELD_AUDIO_BITS_PER_SAMPLE,
    CONFIG_FIELD_AUDIO_CHANNELS,
    CONFIG_FIELD_AUDIO_BCK_PIN,
    CONFIG_FIELD_AUDIO_WS_PIN,
    CONFIG_FIELD_AUDIO_DATA_IN_PIN,

    // Buffer fields
    CONFIG_FIELD_BUFFER_RING_SIZE,
    CONFIG_FIELD_BUFFER_DMA_COUNT,
    CONFIG_FIELD_BUFFER_DMA_LENGTH,
    CONFIG_FIELD_BUFFER_ADAPTIVE_ENABLED,
    CONFIG_FIELD_BUFFER_ADAPTIVE_MIN_SIZE,
    CONFIG_FIELD_BUFFER_ADAPTIVE_MAX_SIZE,
    CONFIG_FIELD_BUFFER_ADAPTIVE_THRESHOLD_LOW,
    CONFIG_FIELD_BUFFER_ADAPTIVE_THRESHOLD_HIGH,
    CONFIG_FIELD_BUFFER_ADAPTIVE_CHECK_INTERVAL,
    CONFIG_FIELD_BUFFER_ADAPTIVE_RESIZE_DELAY,

    // Task fields
    CONFIG_FIELD_TASK_I2S_PRIORITY,
    CONFIG_FIELD_TASK_TCP_PRIORITY,
    CONFIG_FIELD_TASK_WATCHDOG_PRIORITY,
    CONFIG_FIELD_TASK_WEB_PRIORITY,
    CONFIG_FIELD_TASK_I2S_CORE,
    CONFIG_FIELD_TASK_TCP_CORE,
    CONFIG_FIELD_TASK_WATCHDOG_CORE,
    CONFIG_FIELD_TASK_WEB_CORE,

    // Error handling fields
    CONFIG_FIELD_ERROR_MAX_RECONNECT_ATTEMPTS,
    CONFIG_FIELD_ERROR_RECONNECT_BACKOFF_MS,
    CONFIG_FIELD_ERROR_MAX_RECONNECT_BACKOFF_MS,
    CONFIG_FIELD_ERROR_MAX_I2S_FAILURES,
    CONFIG_FIELD_ERROR_MAX_BUFFER_OVERFLOWS,
    CONFIG_FIELD_ERROR_WATCHDOG_TIMEOUT_SEC,
    CONFIG_FIELD_ERROR_NTP_RESYNC_INTERVAL_SEC,

    // Debug fields
    CONFIG_FIELD_DEBUG_ENABLED,
    CONFIG_FIELD_DEBUG_STACK_MONITORING,
    CONFIG_FIELD_DEBUG_AUTO_REBOOT,
    CONFIG_FIELD_DEBUG_I2S_REINIT,
    CONFIG_FIELD_DEBUG_BUFFER_DRAIN,

    // Authentication fields
    CONFIG_FIELD_AUTH_USERNAME,
    CONFIG_FIELD_AUTH_PASSWORD,

    // NTP fields
    CONFIG_FIELD_NTP_SERVER,
    CONFIG_FIELD_NTP_TIMEZONE,

    // UDP fields
    CONFIG_FIELD_UDP_PACKET_MAX_SIZE,
    CONFIG_FIELD_UDP_SEND_TIMEOUT_MS,
    CONFIG_FIELD_UDP_BUFFER_COUNT,
    CONFIG_FIELD_UDP_MULTICAST_GROUP,
    CONFIG_FIELD_UDP_MULTICAST_PORT,

    // TCP optimization fields
    CONFIG_FIELD_TCP_KEEPALIVE_ENABLED,
    CONFIG_FIELD_TCP_KEEPALIVE_IDLE_SEC,
    CONFIG_FIELD_TCP_KEEPALIVE_INTERVAL_SEC,
    CONFIG_FIELD_TCP_KEEPALIVE_COUNT,
    CONFIG_FIELD_TCP_NODELAY_ENABLED,
    CONFIG_FIELD_TCP_TX_BUFFER_SIZE,
    CONFIG_FIELD_TCP_RX_BUFFER_SIZE,

    // Performance monitoring fields
    CONFIG_FIELD_PERF_INTERVAL_MS,
    CONFIG_FIELD_PERF_MAX_ENTRIES,

    // Total number of configuration fields
    CONFIG_FIELD_COUNT
} config_field_id_t;

// Configuration field metadata
typedef struct {
    config_field_id_t id;
    const char* name;
    const char* category;
    uint8_t type; // 0=string, 1=int, 2=uint, 3=bool, 4=float
    uint16_t max_length; // For string fields
    bool restart_required; // Whether change requires restart
    bool volatile_field; // Whether field can change at runtime
} config_field_meta_t;

// Configuration validation result
typedef struct {
    bool valid;
    config_field_id_t field_id;
    char error_message[128];
} config_validation_result_t;

// Unified configuration structure
typedef struct {
    // Network configuration
    char wifi_ssid[32];
    char wifi_password[64];
    bool wifi_use_static_ip;
    char wifi_static_ip[16];
    char wifi_gateway[16];
    char wifi_subnet[16];
    char wifi_dns_primary[16];
    char wifi_dns_secondary[16];

    // Server configuration
    char tcp_server_ip[16];
    uint16_t tcp_server_port;
    char udp_server_ip[16];
    uint16_t udp_server_port;
    uint8_t streaming_protocol; // 0=TCP, 1=UDP, 2=BOTH

    // Audio configuration (fixed values)
    uint32_t audio_sample_rate;    // Fixed: 16000
    uint8_t audio_bits_per_sample; // Fixed: 16
    uint8_t audio_channels;        // Fixed: 1
    uint8_t audio_bck_pin;         // Configurable GPIO
    uint8_t audio_ws_pin;          // Configurable GPIO
    uint8_t audio_data_in_pin;     // Configurable GPIO

    // Buffer configuration
    uint32_t buffer_ring_size;
    uint8_t buffer_dma_count;
    uint16_t buffer_dma_length;
    bool buffer_adaptive_enabled;
    uint32_t buffer_adaptive_min_size;
    uint32_t buffer_adaptive_max_size;
    uint8_t buffer_adaptive_threshold_low;
    uint8_t buffer_adaptive_threshold_high;
    uint32_t buffer_adaptive_check_interval;
    uint32_t buffer_adaptive_resize_delay;

    // Task configuration
    uint8_t task_i2s_priority;
    uint8_t task_tcp_priority;
    uint8_t task_watchdog_priority;
    uint8_t task_web_priority;
    uint8_t task_i2s_core;
    uint8_t task_tcp_core;
    uint8_t task_watchdog_core;
    uint8_t task_web_core;

    // Error handling configuration
    uint16_t error_max_reconnect_attempts;
    uint32_t error_reconnect_backoff_ms;
    uint32_t error_max_reconnect_backoff_ms;
    uint16_t error_max_i2s_failures;
    uint16_t error_max_buffer_overflows;
    uint16_t error_watchdog_timeout_sec;
    uint32_t error_ntp_resync_interval_sec;

    // Debug configuration
    bool debug_enabled;
    bool debug_stack_monitoring;
    bool debug_auto_reboot;
    bool debug_i2s_reinit;
    bool debug_buffer_drain;

    // Authentication configuration
    char auth_username[32];
    char auth_password[64];

    // NTP configuration
    char ntp_server[64];
    char ntp_timezone[32];

    // UDP configuration
    uint32_t udp_packet_max_size;
    uint32_t udp_send_timeout_ms;
    uint8_t udp_buffer_count;
    char udp_multicast_group[16];
    uint16_t udp_multicast_port;

    // TCP optimization configuration
    bool tcp_keepalive_enabled;
    uint32_t tcp_keepalive_idle_sec;
    uint32_t tcp_keepalive_interval_sec;
    uint32_t tcp_keepalive_count;
    bool tcp_nodelay_enabled;
    uint32_t tcp_tx_buffer_size;
    uint32_t tcp_rx_buffer_size;

    // Performance monitoring configuration
    uint32_t perf_interval_ms;
    size_t perf_max_entries;

    // Metadata
    uint8_t version;
    uint32_t last_updated;
} unified_config_t;

/**
 * Get field metadata for a given field ID
 * @param field_id Field identifier
 * @return Field metadata or NULL if not found
 */
const config_field_meta_t* config_schema_get_field_meta(config_field_id_t field_id);

/**
 * Get all fields in a specific category
 * @param category Category name (e.g., "wifi", "audio", "buffer")
 * @param fields Output array for field metadata
 * @param max_fields Maximum number of fields to return
 * @return Number of fields returned
 */
size_t config_schema_get_fields_by_category(const char* category,
                                          const config_field_meta_t** fields,
                                          size_t max_fields);

/**
 * Validate a configuration field value
 * @param field_id Field identifier
 * @param value Value to validate (as string)
 * @param result Validation result output
 * @return true if validation succeeded
 */
bool config_schema_validate_field(config_field_id_t field_id,
                                 const char* value,
                                 config_validation_result_t* result);

/**
 * Validate entire configuration
 * @param config Configuration to validate
 * @param results Array to store validation results
 * @param max_results Maximum number of results to store
 * @return Number of validation issues found (0 = valid)
 */
size_t config_schema_validate_config(const unified_config_t* config,
                                    config_validation_result_t* results,
                                    size_t max_results);

/**
 * Get default value for a field
 * @param field_id Field identifier
 * @param buffer Buffer to store default value
 * @param buffer_size Size of buffer
 * @return true if default value retrieved
 */
bool config_schema_get_field_default(config_field_id_t field_id,
                                    char* buffer,
                                    size_t buffer_size);

/**
 * Set field value in configuration
 * @param config Configuration structure
 * @param field_id Field identifier
 * @param value Value to set (as string)
 * @param result Validation result
 * @return true if value was set successfully
 */
bool config_schema_set_field_value(unified_config_t* config,
                                 config_field_id_t field_id,
                                 const char* value,
                                 config_validation_result_t* result);

/**
 * Get field value from configuration
 * @param config Configuration structure
 * @param field_id Field identifier
 * @param buffer Buffer to store value
 * @param buffer_size Size of buffer
 * @return true if value was retrieved
 */
bool config_schema_get_field_value(const unified_config_t* config,
                                 config_field_id_t field_id,
                                 char* buffer,
                                 size_t buffer_size);

/**
 * Initialize configuration with default values
 * @param config Configuration structure to initialize
 */
void config_schema_init_defaults(unified_config_t* config);

/**
 * Convert legacy configuration to unified format
 * @param legacy_config Legacy configuration data (from old system)
 * @param unified_config Output unified configuration
 * @return true if conversion succeeded
 */
bool config_schema_convert_legacy(const void* legacy_config, unified_config_t* unified_config);

#endif // CONFIG_SCHEMA_H