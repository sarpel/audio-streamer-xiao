#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

// Configuration version for migration
#define CONFIG_VERSION 1

// Configuration structures
typedef struct {
    char ssid[32];
    char password[64];
    bool use_static_ip;
    char static_ip[16];
    char gateway[16];
    char subnet[16];
    char dns_primary[16];
    char dns_secondary[16];
} wifi_config_data_t;

typedef struct {
    char server_ip[16];
    uint16_t server_port;
} tcp_config_data_t;

typedef struct {
    char ntp_server[64];
    char timezone[32];
} ntp_config_data_t;

typedef struct {
    uint32_t sample_rate;
    uint8_t bits_per_sample;
    uint8_t channels;
    uint8_t bck_pin;
    uint8_t ws_pin;
    uint8_t data_in_pin;
    uint8_t audio_format; // audio_format_t
    bool auto_gain_control;
    int8_t input_gain_db; // -40 to +40 dB
} i2s_config_data_t;

typedef struct {
    uint32_t ring_buffer_size;
    uint8_t dma_buf_count;
    uint16_t dma_buf_len;
} buffer_config_data_t;

typedef struct {
    uint8_t i2s_reader_priority;
    uint8_t tcp_sender_priority;
    uint8_t watchdog_priority;
    uint8_t web_server_priority;
    uint8_t i2s_reader_core;
    uint8_t tcp_sender_core;
    uint8_t watchdog_core;
    uint8_t web_server_core;
} task_config_data_t;

typedef struct {
    uint16_t max_reconnect_attempts;
    uint32_t reconnect_backoff_ms;
    uint32_t max_reconnect_backoff_ms;
    uint16_t max_i2s_failures;
    uint16_t max_buffer_overflows;
    uint16_t watchdog_timeout_sec;
    uint32_t ntp_resync_interval_sec;
} error_config_data_t;

typedef struct {
    bool debug_enabled;
    bool stack_monitoring;
    bool auto_reboot;
    bool i2s_reinit;
    bool buffer_drain;
} debug_config_data_t;

typedef struct {
    char username[32];
    char password[64];
} auth_config_data_t;

typedef struct {
    uint8_t protocol; // 0=TCP, 1=UDP, 2=BOTH
} streaming_config_data_t;

typedef struct {
    uint32_t min_size;
    uint32_t max_size;
    uint32_t default_size;
    uint8_t threshold_low;
    uint8_t threshold_high;
    uint32_t check_interval_ms;
    uint32_t resize_delay_ms;
} adaptive_buffer_config_data_t;

typedef struct {
    uint32_t packet_max_size;
    uint32_t send_timeout_ms;
    uint8_t buffer_count;
    char multicast_group[16];
    uint16_t multicast_port;
} udp_config_data_t;

typedef struct {
    bool enabled;
    uint32_t keepalive_idle_sec;
    uint32_t keepalive_interval_sec;
    uint32_t keepalive_count;
    bool nodelay_enabled;
    uint32_t tx_buffer_size;
    uint32_t rx_buffer_size;
} tcp_optimization_config_data_t;

typedef struct {
    uint32_t interval_ms;
    size_t max_entries;
} performance_monitor_config_data_t;

/**
 * Initialize configuration manager and NVS
 * @return true on success
 */
bool config_manager_init(void);

/**
 * Load all configuration from NVS
 * If not found, load defaults from config.h
 * @return true on success
 */
bool config_manager_load(void);

/**
 * Save all configuration to NVS
 * @return true on success
 */
bool config_manager_save(void);

/**
 * Reset all configuration to factory defaults
 * @return true on success
 */
bool config_manager_reset_to_factory(void);

/**
 * Get WiFi configuration
 */
bool config_manager_get_wifi(wifi_config_data_t* config);

/**
 * Set WiFi configuration
 */
bool config_manager_set_wifi(const wifi_config_data_t* config);

/**
 * Get TCP configuration
 */
bool config_manager_get_tcp(tcp_config_data_t* config);

/**
 * Set TCP configuration
 */
bool config_manager_set_tcp(const tcp_config_data_t* config);

/**
 * Get NTP configuration
 */
bool config_manager_get_ntp(ntp_config_data_t* config);

/**
 * Set NTP configuration
 */
bool config_manager_set_ntp(const ntp_config_data_t* config);

/**
 * Get I2S configuration
 */
bool config_manager_get_i2s(i2s_config_data_t* config);

/**
 * Set I2S configuration
 */
bool config_manager_set_i2s(const i2s_config_data_t* config);

/**
 * Get buffer configuration
 */
bool config_manager_get_buffer(buffer_config_data_t* config);

/**
 * Set buffer configuration
 */
bool config_manager_set_buffer(const buffer_config_data_t* config);

/**
 * Get task configuration
 */
bool config_manager_get_tasks(task_config_data_t* config);

/**
 * Set task configuration
 */
bool config_manager_set_tasks(const task_config_data_t* config);

/**
 * Get error handling configuration
 */
bool config_manager_get_error(error_config_data_t* config);

/**
 * Set error handling configuration
 */
bool config_manager_set_error(const error_config_data_t* config);

/**
 * Get debug configuration
 */
bool config_manager_get_debug(debug_config_data_t* config);

/**
 * Set debug configuration
 */
bool config_manager_set_debug(const debug_config_data_t* config);

/**
 * Get authentication configuration
 */
bool config_manager_get_auth(auth_config_data_t* config);

/**
 * Set authentication configuration
 */
bool config_manager_set_auth(const auth_config_data_t* config);

/**
 * Get streaming configuration
 */
bool config_manager_get_streaming(streaming_config_data_t* config);

/**
 * Set streaming configuration
 */
bool config_manager_set_streaming(const streaming_config_data_t* config);

/**
 * Get adaptive buffer configuration
 */
bool config_manager_get_adaptive_buffer(adaptive_buffer_config_data_t* config);

/**
 * Set adaptive buffer configuration
 */
bool config_manager_set_adaptive_buffer(const adaptive_buffer_config_data_t* config);

/**
 * Get UDP configuration
 */
bool config_manager_get_udp(udp_config_data_t* config);

/**
 * Set UDP configuration
 */
bool config_manager_set_udp(const udp_config_data_t* config);

/**
 * Get TCP optimization configuration
 */
bool config_manager_get_tcp_optimization(tcp_optimization_config_data_t* config);

/**
 * Set TCP optimization configuration
 */
bool config_manager_set_tcp_optimization(const tcp_optimization_config_data_t* config);

/**
 * Get performance monitor configuration
 */
bool config_manager_get_performance_monitor(performance_monitor_config_data_t* config);

/**
 * Set performance monitor configuration
 */
bool config_manager_set_performance_monitor(const performance_monitor_config_data_t* config);

/**
 * Check if this is first boot (no config in NVS)
 */
bool config_manager_is_first_boot(void);

/**
 * Export all configuration to JSON string
 * @param json_output Buffer to store JSON output (must be at least 4096 bytes)
 * @param buffer_size Size of json_output buffer
 * @return true on success, false if buffer too small
 */
bool config_manager_export_json(char* json_output, size_t buffer_size);

/**
 * Import configuration from JSON string
 * @param json_input JSON string with configuration data
 * @param overwrite Whether to overwrite existing configuration (true) or merge (false)
 * @return true on success, false on JSON parse error
 */
bool config_manager_import_json(const char* json_input, bool overwrite);

/**
 * Validate configuration data integrity
 * @return true if configuration is valid
 */
bool config_manager_validate(void);

/**
 * Get configuration version
 * @return Current configuration version
 */
uint8_t config_manager_get_version(void);

/**
 * Check if configuration needs migration
 * @param from_version Version to migrate from
 * @return true if migration is needed
 */
bool config_manager_needs_migration(uint8_t from_version);

/**
 * Migrate configuration from older version
 * @param from_version Source version
 * @return true on success
 */
bool config_manager_migrate(uint8_t from_version);

/**
 * Validate audio format configuration
 * @param sample_rate Sample rate in Hz
 * @param bits_per_sample Bits per sample (8, 16, 24, 32)
 * @param channels Number of channels (1 or 2)
 * @return true if valid
 */
bool config_manager_validate_audio_format(uint32_t sample_rate, uint8_t bits_per_sample, uint8_t channels);

/**
 * Get audio format name from enum
 * @param format Audio format enum
 * @param buffer Buffer to store name (must be at least 16 bytes)
 * @return true on success
 */
bool config_manager_get_audio_format_name(uint8_t format, char *buffer, size_t buffer_size);

/**
 * Get sample rate name from value
 * @param sample_rate Sample rate in Hz
 * @param buffer Buffer to store name (must be at least 16 bytes)
 * @return true on success
 */
bool config_manager_get_sample_rate_name(uint32_t sample_rate, char *buffer, size_t buffer_size);

/**
 * Calculate audio data rate
 * @param sample_rate Sample rate in Hz
 * @param bits_per_sample Bits per sample
 * @param channels Number of channels
 * @return Data rate in bits per second
 */
uint32_t config_manager_calculate_audio_data_rate(uint32_t sample_rate, uint8_t bits_per_sample, uint8_t channels);

/**
 * Convert audio format to bits per sample
 * @param format Audio format enum
 * @return Bits per sample
 */
uint8_t config_manager_format_to_bits_per_sample(uint8_t format);

/**
 * Convert bits per sample to audio format
 * @param bits_per_sample Bits per sample
 * @return Audio format enum (AUDIO_FORMAT_PCM_16BIT if invalid)
 */
uint8_t config_manager_bits_per_sample_to_format(uint8_t bits_per_sample);

/**
 * Deinitialize config manager
 */
void config_manager_deinit(void);

#endif // CONFIG_MANAGER_H
