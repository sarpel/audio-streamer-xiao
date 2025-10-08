#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

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
 * Check if this is first boot (no config in NVS)
 */
bool config_manager_is_first_boot(void);

/**
 * Deinitialize config manager
 */
void config_manager_deinit(void);

#endif // CONFIG_MANAGER_H
