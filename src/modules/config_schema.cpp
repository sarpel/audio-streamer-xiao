#include "config_schema.h"
#include "../config.h"
#include "performance_monitor.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

// static const char* TAG = "CONFIG_SCHEMA"; // Uncomment when adding logging

// Field metadata table
static const config_field_meta_t field_meta[CONFIG_FIELD_COUNT] = {
    // Network fields
    {CONFIG_FIELD_WIFI_SSID, "wifi_ssid", "network", 0, 32, true, false},
    {CONFIG_FIELD_WIFI_PASSWORD, "wifi_password", "network", 0, 64, true, false},
    {CONFIG_FIELD_WIFI_USE_STATIC_IP, "wifi_use_static_ip", "network", 3, 0, true, false},
    {CONFIG_FIELD_WIFI_STATIC_IP, "wifi_static_ip", "network", 0, 16, true, false},
    {CONFIG_FIELD_WIFI_GATEWAY, "wifi_gateway", "network", 0, 16, true, false},
    {CONFIG_FIELD_WIFI_SUBNET, "wifi_subnet", "network", 0, 16, true, false},
    {CONFIG_FIELD_WIFI_DNS_PRIMARY, "wifi_dns_primary", "network", 0, 16, true, false},
    {CONFIG_FIELD_WIFI_DNS_SECONDARY, "wifi_dns_secondary", "network", 0, 16, true, false},

    // Server fields
    {CONFIG_FIELD_TCP_SERVER_IP, "tcp_server_ip", "server", 0, 16, true, false},
    {CONFIG_FIELD_TCP_SERVER_PORT, "tcp_server_port", "server", 2, 0, true, false},
    {CONFIG_FIELD_UDP_SERVER_IP, "udp_server_ip", "server", 0, 16, true, false},
    {CONFIG_FIELD_UDP_SERVER_PORT, "udp_server_port", "server", 2, 0, true, false},
    {CONFIG_FIELD_STREAMING_PROTOCOL, "streaming_protocol", "server", 2, 0, true, false},

    // Audio fields
    {CONFIG_FIELD_AUDIO_SAMPLE_RATE, "audio_sample_rate", "audio", 2, 0, true, false},
    {CONFIG_FIELD_AUDIO_BITS_PER_SAMPLE, "audio_bits_per_sample", "audio", 2, 0, true, false},
    {CONFIG_FIELD_AUDIO_CHANNELS, "audio_channels", "audio", 2, 0, true, false},
    {CONFIG_FIELD_AUDIO_BCK_PIN, "audio_bck_pin", "audio", 2, 0, true, false},
    {CONFIG_FIELD_AUDIO_WS_PIN, "audio_ws_pin", "audio", 2, 0, true, false},
    {CONFIG_FIELD_AUDIO_DATA_IN_PIN, "audio_data_in_pin", "audio", 2, 0, true, false},

    // Buffer fields
    {CONFIG_FIELD_BUFFER_RING_SIZE, "buffer_ring_size", "buffer", 2, 0, true, false},
    {CONFIG_FIELD_BUFFER_DMA_COUNT, "buffer_dma_count", "buffer", 2, 0, true, false},
    {CONFIG_FIELD_BUFFER_DMA_LENGTH, "buffer_dma_length", "buffer", 2, 0, true, false},
    {CONFIG_FIELD_BUFFER_ADAPTIVE_ENABLED, "buffer_adaptive_enabled", "buffer", 3, 0, true, false},
    {CONFIG_FIELD_BUFFER_ADAPTIVE_MIN_SIZE, "buffer_adaptive_min_size", "buffer", 2, 0, true, false},
    {CONFIG_FIELD_BUFFER_ADAPTIVE_MAX_SIZE, "buffer_adaptive_max_size", "buffer", 2, 0, true, false},
    {CONFIG_FIELD_BUFFER_ADAPTIVE_THRESHOLD_LOW, "buffer_adaptive_threshold_low", "buffer", 2, 0, true, false},
    {CONFIG_FIELD_BUFFER_ADAPTIVE_THRESHOLD_HIGH, "buffer_adaptive_threshold_high", "buffer", 2, 0, true, false},
    {CONFIG_FIELD_BUFFER_ADAPTIVE_CHECK_INTERVAL, "buffer_adaptive_check_interval", "buffer", 2, 0, true, false},
    {CONFIG_FIELD_BUFFER_ADAPTIVE_RESIZE_DELAY, "buffer_adaptive_resize_delay", "buffer", 2, 0, true, false},

    // Task fields
    {CONFIG_FIELD_TASK_I2S_PRIORITY, "task_i2s_priority", "task", 2, 0, true, false},
    {CONFIG_FIELD_TASK_TCP_PRIORITY, "task_tcp_priority", "task", 2, 0, true, false},
    {CONFIG_FIELD_TASK_WATCHDOG_PRIORITY, "task_watchdog_priority", "task", 2, 0, true, false},
    {CONFIG_FIELD_TASK_WEB_PRIORITY, "task_web_priority", "task", 2, 0, true, false},
    {CONFIG_FIELD_TASK_I2S_CORE, "task_i2s_core", "task", 2, 0, true, false},
    {CONFIG_FIELD_TASK_TCP_CORE, "task_tcp_core", "task", 2, 0, true, false},
    {CONFIG_FIELD_TASK_WATCHDOG_CORE, "task_watchdog_core", "task", 2, 0, true, false},
    {CONFIG_FIELD_TASK_WEB_CORE, "task_web_core", "task", 2, 0, true, false},

    // Error handling fields
    {CONFIG_FIELD_ERROR_MAX_RECONNECT_ATTEMPTS, "error_max_reconnect_attempts", "error", 2, 0, false, false},
    {CONFIG_FIELD_ERROR_RECONNECT_BACKOFF_MS, "error_reconnect_backoff_ms", "error", 2, 0, false, false},
    {CONFIG_FIELD_ERROR_MAX_RECONNECT_BACKOFF_MS, "error_max_reconnect_backoff_ms", "error", 2, 0, false, false},
    {CONFIG_FIELD_ERROR_MAX_I2S_FAILURES, "error_max_i2s_failures", "error", 2, 0, false, false},
    {CONFIG_FIELD_ERROR_MAX_BUFFER_OVERFLOWS, "error_max_buffer_overflows", "error", 2, 0, false, false},
    {CONFIG_FIELD_ERROR_WATCHDOG_TIMEOUT_SEC, "error_watchdog_timeout_sec", "error", 2, 0, false, false},
    {CONFIG_FIELD_ERROR_NTP_RESYNC_INTERVAL_SEC, "error_ntp_resync_interval_sec", "error", 2, 0, false, false},

    // Debug fields
    {CONFIG_FIELD_DEBUG_ENABLED, "debug_enabled", "debug", 3, 0, false, true},
    {CONFIG_FIELD_DEBUG_STACK_MONITORING, "debug_stack_monitoring", "debug", 3, 0, false, true},
    {CONFIG_FIELD_DEBUG_AUTO_REBOOT, "debug_auto_reboot", "debug", 3, 0, false, true},
    {CONFIG_FIELD_DEBUG_I2S_REINIT, "debug_i2s_reinit", "debug", 3, 0, false, true},
    {CONFIG_FIELD_DEBUG_BUFFER_DRAIN, "debug_buffer_drain", "debug", 3, 0, false, true},

    // Authentication fields
    {CONFIG_FIELD_AUTH_USERNAME, "auth_username", "auth", 0, 32, true, false},
    {CONFIG_FIELD_AUTH_PASSWORD, "auth_password", "auth", 0, 64, true, false},

    // NTP fields
    {CONFIG_FIELD_NTP_SERVER, "ntp_server", "ntp", 0, 64, true, false},
    {CONFIG_FIELD_NTP_TIMEZONE, "ntp_timezone", "ntp", 0, 32, true, false},

    // UDP fields
    {CONFIG_FIELD_UDP_PACKET_MAX_SIZE, "udp_packet_max_size", "udp", 2, 0, true, false},
    {CONFIG_FIELD_UDP_SEND_TIMEOUT_MS, "udp_send_timeout_ms", "udp", 2, 0, true, false},
    {CONFIG_FIELD_UDP_BUFFER_COUNT, "udp_buffer_count", "udp", 2, 0, true, false},
    {CONFIG_FIELD_UDP_MULTICAST_GROUP, "udp_multicast_group", "udp", 0, 16, true, false},
    {CONFIG_FIELD_UDP_MULTICAST_PORT, "udp_multicast_port", "udp", 2, 0, true, false},

    // TCP optimization fields
    {CONFIG_FIELD_TCP_KEEPALIVE_ENABLED, "tcp_keepalive_enabled", "tcp", 3, 0, true, false},
    {CONFIG_FIELD_TCP_KEEPALIVE_IDLE_SEC, "tcp_keepalive_idle_sec", "tcp", 2, 0, true, false},
    {CONFIG_FIELD_TCP_KEEPALIVE_INTERVAL_SEC, "tcp_keepalive_interval_sec", "tcp", 2, 0, true, false},
    {CONFIG_FIELD_TCP_KEEPALIVE_COUNT, "tcp_keepalive_count", "tcp", 2, 0, true, false},
    {CONFIG_FIELD_TCP_NODELAY_ENABLED, "tcp_nodelay_enabled", "tcp", 3, 0, true, false},
    {CONFIG_FIELD_TCP_TX_BUFFER_SIZE, "tcp_tx_buffer_size", "tcp", 2, 0, true, false},
    {CONFIG_FIELD_TCP_RX_BUFFER_SIZE, "tcp_rx_buffer_size", "tcp", 2, 0, true, false},

    // Performance monitoring fields
    {CONFIG_FIELD_PERF_INTERVAL_MS, "perf_interval_ms", "performance", 2, 0, false, true},
    {CONFIG_FIELD_PERF_MAX_ENTRIES, "perf_max_entries", "performance", 2, 0, false, true},
};

// Helper function to validate IP address
static bool is_valid_ip(const char *ip)
{
    if (!ip || strlen(ip) == 0)
        return false;

    int octets[4];
    if (sscanf(ip, "%d.%d.%d.%d", &octets[0], &octets[1], &octets[2], &octets[3]) != 4)
    {
        return false;
    }

    for (int i = 0; i < 4; i++)
    {
        if (octets[i] < 0 || octets[i] > 255)
        {
            return false;
        }
    }

    return true;
}

// Helper function to validate port number
static bool is_valid_port(uint16_t port)
{
    return port >= 1; // uint16_t max is 65535, no need to check upper bound
}

// Helper function to validate GPIO pin
static bool is_valid_gpio(uint8_t gpio)
{
    // Valid GPIO pins for ESP32-S3 (excluding strapping pins)
    return gpio <= 48 || (gpio >= 100 && gpio <= 111);
}

const config_field_meta_t *config_schema_get_field_meta(config_field_id_t field_id)
{
    if (field_id >= CONFIG_FIELD_COUNT)
    {
        return NULL;
    }
    return &field_meta[field_id];
}

size_t config_schema_get_fields_by_category(const char *category,
                                            const config_field_meta_t **fields,
                                            size_t max_fields)
{
    if (!category || !fields || max_fields == 0)
    {
        return 0;
    }

    size_t count = 0;
    for (size_t i = 0; i < CONFIG_FIELD_COUNT && count < max_fields; i++)
    {
        if (strcmp(field_meta[i].category, category) == 0)
        {
            fields[count++] = &field_meta[i];
        }
    }

    return count;
}

bool config_schema_validate_field(config_field_id_t field_id,
                                  const char *value,
                                  config_validation_result_t *result)
{
    if (!result)
    {
        return false;
    }

    result->field_id = field_id;
    result->valid = false;
    strcpy(result->error_message, "Unknown field");

    const config_field_meta_t *meta = config_schema_get_field_meta(field_id);
    if (!meta)
    {
        return false;
    }

    // Check string length
    if (meta->type == 0 && strlen(value) >= meta->max_length)
    {
        snprintf(result->error_message, sizeof(result->error_message),
                 "Value too long (max %d characters)", meta->max_length - 1);
        return false;
    }

    // Field-specific validation
    switch (field_id)
    {
    case CONFIG_FIELD_WIFI_SSID:
        if (strlen(value) == 0)
        {
            strcpy(result->error_message, "SSID cannot be empty");
            return false;
        }
        result->valid = true;
        strcpy(result->error_message, "Valid SSID");
        break;

    case CONFIG_FIELD_WIFI_STATIC_IP:
    case CONFIG_FIELD_WIFI_GATEWAY:
    case CONFIG_FIELD_WIFI_SUBNET:
    case CONFIG_FIELD_WIFI_DNS_PRIMARY:
    case CONFIG_FIELD_WIFI_DNS_SECONDARY:
    case CONFIG_FIELD_TCP_SERVER_IP:
    case CONFIG_FIELD_UDP_SERVER_IP:
    case CONFIG_FIELD_UDP_MULTICAST_GROUP:
        if (!is_valid_ip(value))
        {
            strcpy(result->error_message, "Invalid IP address format");
            return false;
        }
        result->valid = true;
        strcpy(result->error_message, "Valid IP address");
        break;

    case CONFIG_FIELD_TCP_SERVER_PORT:
    case CONFIG_FIELD_UDP_SERVER_PORT:
    case CONFIG_FIELD_UDP_MULTICAST_PORT:
    {
        uint16_t port = (uint16_t)strtoul(value, NULL, 10);
        if (!is_valid_port(port))
        {
            strcpy(result->error_message, "Port must be between 1 and 65535");
            return false;
        }
        result->valid = true;
        strcpy(result->error_message, "Valid port number");
        break;
    }

    case CONFIG_FIELD_AUDIO_SAMPLE_RATE:
    {
        uint32_t rate = strtoul(value, NULL, 10);
        if (rate != SAMPLE_RATE)
        {
            snprintf(result->error_message, sizeof(result->error_message),
                     "Sample rate must be %d Hz (fixed)", SAMPLE_RATE);
            return false;
        }
        result->valid = true;
        strcpy(result->error_message, "Valid sample rate");
        break;
    }

    case CONFIG_FIELD_AUDIO_BITS_PER_SAMPLE:
    {
        uint8_t bits = (uint8_t)strtoul(value, NULL, 10);
        if (bits != BITS_PER_SAMPLE)
        {
            snprintf(result->error_message, sizeof(result->error_message),
                     "Bits per sample must be %d (fixed)", BITS_PER_SAMPLE);
            return false;
        }
        result->valid = true;
        strcpy(result->error_message, "Valid bits per sample");
        break;
    }

    case CONFIG_FIELD_AUDIO_CHANNELS:
    {
        uint8_t channels = (uint8_t)strtoul(value, NULL, 10);
        if (channels != CHANNELS)
        {
            snprintf(result->error_message, sizeof(result->error_message),
                     "Channels must be %d (fixed)", CHANNELS);
            return false;
        }
        result->valid = true;
        strcpy(result->error_message, "Valid channel count");
        break;
    }

    case CONFIG_FIELD_AUDIO_BCK_PIN:
    case CONFIG_FIELD_AUDIO_WS_PIN:
    case CONFIG_FIELD_AUDIO_DATA_IN_PIN:
    {
        uint8_t gpio = (uint8_t)strtoul(value, NULL, 10);
        if (!is_valid_gpio(gpio))
        {
            strcpy(result->error_message, "Invalid GPIO pin number");
            return false;
        }
        result->valid = true;
        strcpy(result->error_message, "Valid GPIO pin");
        break;
    }

    case CONFIG_FIELD_STREAMING_PROTOCOL:
    {
        uint8_t protocol = (uint8_t)strtoul(value, NULL, 10);
        if (protocol > 2)
        {
            strcpy(result->error_message, "Protocol must be 0 (TCP), 1 (UDP), or 2 (BOTH)");
            return false;
        }
        result->valid = true;
        strcpy(result->error_message, "Valid streaming protocol");
        break;
    }

    default:
        // For other fields, just check basic format
        result->valid = true;
        strcpy(result->error_message, "Valid field value");
        break;
    }

    return result->valid;
}

size_t config_schema_validate_config(const unified_config_t *config,
                                     config_validation_result_t *results,
                                     size_t max_results)
{
    if (!config || !results || max_results == 0)
    {
        return 0;
    }

    size_t issue_count = 0;

    // Validate critical network configuration
    if (strlen(config->wifi_ssid) == 0)
    {
        results[issue_count].field_id = CONFIG_FIELD_WIFI_SSID;
        results[issue_count].valid = false;
        strcpy(results[issue_count].error_message, "WiFi SSID cannot be empty");
        issue_count++;
        if (issue_count >= max_results)
            return issue_count;
    }

    // Validate server IP addresses
    if (!is_valid_ip(config->tcp_server_ip))
    {
        results[issue_count].field_id = CONFIG_FIELD_TCP_SERVER_IP;
        results[issue_count].valid = false;
        strcpy(results[issue_count].error_message, "Invalid TCP server IP address");
        issue_count++;
        if (issue_count >= max_results)
            return issue_count;
    }

    if (!is_valid_ip(config->udp_server_ip))
    {
        results[issue_count].field_id = CONFIG_FIELD_UDP_SERVER_IP;
        results[issue_count].valid = false;
        strcpy(results[issue_count].error_message, "Invalid UDP server IP address");
        issue_count++;
        if (issue_count >= max_results)
            return issue_count;
    }

    // Validate port numbers
    if (!is_valid_port(config->tcp_server_port))
    {
        results[issue_count].field_id = CONFIG_FIELD_TCP_SERVER_PORT;
        results[issue_count].valid = false;
        strcpy(results[issue_count].error_message, "Invalid TCP server port");
        issue_count++;
        if (issue_count >= max_results)
            return issue_count;
    }

    if (!is_valid_port(config->udp_server_port))
    {
        results[issue_count].field_id = CONFIG_FIELD_UDP_SERVER_PORT;
        results[issue_count].valid = false;
        strcpy(results[issue_count].error_message, "Invalid UDP server port");
        issue_count++;
        if (issue_count >= max_results)
            return issue_count;
    }

    // Validate audio GPIO pins
    if (!is_valid_gpio(config->audio_bck_pin))
    {
        results[issue_count].field_id = CONFIG_FIELD_AUDIO_BCK_PIN;
        results[issue_count].valid = false;
        strcpy(results[issue_count].error_message, "Invalid BCK GPIO pin");
        issue_count++;
        if (issue_count >= max_results)
            return issue_count;
    }

    if (!is_valid_gpio(config->audio_ws_pin))
    {
        results[issue_count].field_id = CONFIG_FIELD_AUDIO_WS_PIN;
        results[issue_count].valid = false;
        strcpy(results[issue_count].error_message, "Invalid WS GPIO pin");
        issue_count++;
        if (issue_count >= max_results)
            return issue_count;
    }

    if (!is_valid_gpio(config->audio_data_in_pin))
    {
        results[issue_count].field_id = CONFIG_FIELD_AUDIO_DATA_IN_PIN;
        results[issue_count].valid = false;
        strcpy(results[issue_count].error_message, "Invalid Data In GPIO pin");
        issue_count++;
        if (issue_count >= max_results)
            return issue_count;
    }

    return issue_count;
}

bool config_schema_get_field_default(config_field_id_t field_id,
                                     char *buffer,
                                     size_t buffer_size)
{
    if (!buffer || buffer_size == 0)
    {
        return false;
    }

    switch (field_id)
    {
    // Network defaults
    case CONFIG_FIELD_WIFI_SSID:
        strncpy(buffer, WIFI_SSID, buffer_size - 1);
        break;
    case CONFIG_FIELD_WIFI_PASSWORD:
        strncpy(buffer, WIFI_PASSWORD, buffer_size - 1);
        break;
    case CONFIG_FIELD_WIFI_USE_STATIC_IP:
        strncpy(buffer, "0", buffer_size - 1); // DHCP by default
        break;
    case CONFIG_FIELD_WIFI_STATIC_IP:
    case CONFIG_FIELD_WIFI_GATEWAY:
    case CONFIG_FIELD_WIFI_SUBNET:
    case CONFIG_FIELD_WIFI_DNS_PRIMARY:
    case CONFIG_FIELD_WIFI_DNS_SECONDARY:
        strncpy(buffer, "0.0.0.0", buffer_size - 1);
        break;

    // Server defaults
    case CONFIG_FIELD_TCP_SERVER_IP:
    case CONFIG_FIELD_UDP_SERVER_IP:
        strncpy(buffer, TCP_SERVER_IP, buffer_size - 1);
        break;
    case CONFIG_FIELD_TCP_SERVER_PORT:
        strncpy(buffer, "9000", buffer_size - 1);
        break;
    case CONFIG_FIELD_UDP_SERVER_PORT:
        strncpy(buffer, "9001", buffer_size - 1);
        break;
    case CONFIG_FIELD_STREAMING_PROTOCOL:
        strncpy(buffer, "0", buffer_size - 1); // TCP by default
        break;

    // Audio defaults (fixed values)
    case CONFIG_FIELD_AUDIO_SAMPLE_RATE:
        snprintf(buffer, buffer_size, "%d", SAMPLE_RATE);
        break;
    case CONFIG_FIELD_AUDIO_BITS_PER_SAMPLE:
        snprintf(buffer, buffer_size, "%d", BITS_PER_SAMPLE);
        break;
    case CONFIG_FIELD_AUDIO_CHANNELS:
        snprintf(buffer, buffer_size, "%d", CHANNELS);
        break;
    case CONFIG_FIELD_AUDIO_BCK_PIN:
        snprintf(buffer, buffer_size, "%d", I2S_BCLK_GPIO);
        break;
    case CONFIG_FIELD_AUDIO_WS_PIN:
        snprintf(buffer, buffer_size, "%d", I2S_WS_GPIO);
        break;
    case CONFIG_FIELD_AUDIO_DATA_IN_PIN:
        snprintf(buffer, buffer_size, "%d", I2S_SD_GPIO);
        break;

    // Buffer defaults
    case CONFIG_FIELD_BUFFER_RING_SIZE:
        snprintf(buffer, buffer_size, "%d", RING_BUFFER_SIZE);
        break;
    case CONFIG_FIELD_BUFFER_DMA_COUNT:
        snprintf(buffer, buffer_size, "%d", I2S_DMA_BUF_COUNT);
        break;
    case CONFIG_FIELD_BUFFER_DMA_LENGTH:
        snprintf(buffer, buffer_size, "%d", I2S_DMA_BUF_LEN);
        break;
    case CONFIG_FIELD_BUFFER_ADAPTIVE_ENABLED:
        strncpy(buffer, "1", buffer_size - 1); // Enabled by default
        break;
    case CONFIG_FIELD_BUFFER_ADAPTIVE_MIN_SIZE:
        snprintf(buffer, buffer_size, "%d", ADAPTIVE_BUFFER_MIN_SIZE);
        break;
    case CONFIG_FIELD_BUFFER_ADAPTIVE_MAX_SIZE:
        snprintf(buffer, buffer_size, "%d", ADAPTIVE_BUFFER_MAX_SIZE);
        break;
    case CONFIG_FIELD_BUFFER_ADAPTIVE_THRESHOLD_LOW:
        snprintf(buffer, buffer_size, "%d", ADAPTIVE_THRESHOLD_LOW);
        break;
    case CONFIG_FIELD_BUFFER_ADAPTIVE_THRESHOLD_HIGH:
        snprintf(buffer, buffer_size, "%d", ADAPTIVE_THRESHOLD_HIGH);
        break;
    case CONFIG_FIELD_BUFFER_ADAPTIVE_CHECK_INTERVAL:
        snprintf(buffer, buffer_size, "%d", ADAPTIVE_CHECK_INTERVAL_MS);
        break;
    case CONFIG_FIELD_BUFFER_ADAPTIVE_RESIZE_DELAY:
        snprintf(buffer, buffer_size, "%d", ADAPTIVE_RESIZE_DELAY_MS);
        break;

    // Task defaults
    case CONFIG_FIELD_TASK_I2S_PRIORITY:
        snprintf(buffer, buffer_size, "%d", I2S_READER_PRIORITY);
        break;
    case CONFIG_FIELD_TASK_TCP_PRIORITY:
        snprintf(buffer, buffer_size, "%d", TCP_SENDER_PRIORITY);
        break;
    case CONFIG_FIELD_TASK_WATCHDOG_PRIORITY:
        snprintf(buffer, buffer_size, "%d", WATCHDOG_PRIORITY);
        break;
    case CONFIG_FIELD_TASK_WEB_PRIORITY:
        snprintf(buffer, buffer_size, "%d", 5); // Default web server priority
        break;
    case CONFIG_FIELD_TASK_I2S_CORE:
        snprintf(buffer, buffer_size, "%d", I2S_READER_CORE);
        break;
    case CONFIG_FIELD_TASK_TCP_CORE:
        snprintf(buffer, buffer_size, "%d", TCP_SENDER_CORE);
        break;
    case CONFIG_FIELD_TASK_WATCHDOG_CORE:
        snprintf(buffer, buffer_size, "%d", WATCHDOG_CORE);
        break;
    case CONFIG_FIELD_TASK_WEB_CORE:
        snprintf(buffer, buffer_size, "%d", 0); // Web server on core 0
        break;

    // Error handling defaults
    case CONFIG_FIELD_ERROR_MAX_RECONNECT_ATTEMPTS:
        snprintf(buffer, buffer_size, "%d", MAX_RECONNECT_ATTEMPTS);
        break;
    case CONFIG_FIELD_ERROR_RECONNECT_BACKOFF_MS:
        snprintf(buffer, buffer_size, "%d", RECONNECT_BACKOFF_MS);
        break;
    case CONFIG_FIELD_ERROR_MAX_RECONNECT_BACKOFF_MS:
        snprintf(buffer, buffer_size, "%d", MAX_RECONNECT_BACKOFF_MS);
        break;
    case CONFIG_FIELD_ERROR_MAX_I2S_FAILURES:
        snprintf(buffer, buffer_size, "%d", MAX_I2S_FAILURES);
        break;
    case CONFIG_FIELD_ERROR_MAX_BUFFER_OVERFLOWS:
        snprintf(buffer, buffer_size, "%d", MAX_BUFFER_OVERFLOWS);
        break;
    case CONFIG_FIELD_ERROR_WATCHDOG_TIMEOUT_SEC:
        snprintf(buffer, buffer_size, "%d", WATCHDOG_TIMEOUT_SEC);
        break;
    case CONFIG_FIELD_ERROR_NTP_RESYNC_INTERVAL_SEC:
        snprintf(buffer, buffer_size, "%d", NTP_RESYNC_INTERVAL_SEC);
        break;

    // Debug defaults
    case CONFIG_FIELD_DEBUG_ENABLED:
        strncpy(buffer, DEBUG_ENABLED ? "1" : "0", buffer_size - 1);
        break;
    case CONFIG_FIELD_DEBUG_STACK_MONITORING:
        strncpy(buffer, ENABLE_STACK_MONITORING ? "1" : "0", buffer_size - 1);
        break;
    case CONFIG_FIELD_DEBUG_AUTO_REBOOT:
        strncpy(buffer, ENABLE_AUTO_REBOOT ? "1" : "0", buffer_size - 1);
        break;
    case CONFIG_FIELD_DEBUG_I2S_REINIT:
        strncpy(buffer, ENABLE_I2S_REINIT ? "1" : "0", buffer_size - 1);
        break;
    case CONFIG_FIELD_DEBUG_BUFFER_DRAIN:
        strncpy(buffer, ENABLE_BUFFER_DRAIN ? "1" : "0", buffer_size - 1);
        break;

    // Authentication defaults
    case CONFIG_FIELD_AUTH_USERNAME:
        strncpy(buffer, WEB_AUTH_USERNAME, buffer_size - 1);
        break;
    case CONFIG_FIELD_AUTH_PASSWORD:
        strncpy(buffer, WEB_AUTH_PASSWORD, buffer_size - 1);
        break;

    // NTP defaults
    case CONFIG_FIELD_NTP_SERVER:
        strncpy(buffer, NTP_SERVER, buffer_size - 1);
        break;
    case CONFIG_FIELD_NTP_TIMEZONE:
        strncpy(buffer, NTP_TIMEZONE, buffer_size - 1);
        break;

    // UDP defaults
    case CONFIG_FIELD_UDP_PACKET_MAX_SIZE:
        snprintf(buffer, buffer_size, "%d", UDP_PACKET_MAX_SIZE);
        break;
    case CONFIG_FIELD_UDP_SEND_TIMEOUT_MS:
        snprintf(buffer, buffer_size, "%d", UDP_SEND_TIMEOUT_MS);
        break;
    case CONFIG_FIELD_UDP_BUFFER_COUNT:
        snprintf(buffer, buffer_size, "%d", UDP_BUFFER_COUNT);
        break;
    case CONFIG_FIELD_UDP_MULTICAST_GROUP:
        strncpy(buffer, "239.255.1.1", buffer_size - 1);
        break;
    case CONFIG_FIELD_UDP_MULTICAST_PORT:
        strncpy(buffer, "9002", buffer_size - 1);
        break;

    // TCP optimization defaults
    case CONFIG_FIELD_TCP_KEEPALIVE_ENABLED:
        strncpy(buffer, TCP_KEEPALIVE_ENABLED ? "1" : "0", buffer_size - 1);
        break;
    case CONFIG_FIELD_TCP_KEEPALIVE_IDLE_SEC:
        snprintf(buffer, buffer_size, "%d", TCP_KEEPALIVE_IDLE_SEC);
        break;
    case CONFIG_FIELD_TCP_KEEPALIVE_INTERVAL_SEC:
        snprintf(buffer, buffer_size, "%d", TCP_KEEPALIVE_INTERVAL_SEC);
        break;
    case CONFIG_FIELD_TCP_KEEPALIVE_COUNT:
        snprintf(buffer, buffer_size, "%d", TCP_KEEPALIVE_COUNT);
        break;
    case CONFIG_FIELD_TCP_NODELAY_ENABLED:
        strncpy(buffer, TCP_NODELAY_ENABLED ? "1" : "0", buffer_size - 1);
        break;
    case CONFIG_FIELD_TCP_TX_BUFFER_SIZE:
        snprintf(buffer, buffer_size, "%d", TCP_TX_BUFFER_SIZE);
        break;
    case CONFIG_FIELD_TCP_RX_BUFFER_SIZE:
        snprintf(buffer, buffer_size, "%d", TCP_RX_BUFFER_SIZE);
        break;

    // Performance monitoring defaults
    case CONFIG_FIELD_PERF_INTERVAL_MS:
        snprintf(buffer, buffer_size, "%d", HISTORY_INTERVAL_MS);
        break;
    case CONFIG_FIELD_PERF_MAX_ENTRIES:
        snprintf(buffer, buffer_size, "%d", MAX_HISTORY_ENTRIES);
        break;

    default:
        return false;
    }

    buffer[buffer_size - 1] = '\0';
    return true;
}

void config_schema_init_defaults(unified_config_t *config)
{
    if (!config)
    {
        return;
    }

    // Initialize all fields to zero
    memset(config, 0, sizeof(unified_config_t));

    // Set default values
    char default_value[64];
    for (int i = 0; i < CONFIG_FIELD_COUNT; i++)
    {
        if (config_schema_get_field_default((config_field_id_t)i, default_value, sizeof(default_value)))
        {
            config_schema_set_field_value(config, (config_field_id_t)i, default_value, NULL);
        }
    }

    // Set metadata
    config->version = CONFIG_SCHEMA_VERSION;
    config->last_updated = 0; // Will be set when saved
}

bool config_schema_set_field_value(unified_config_t *config,
                                   config_field_id_t field_id,
                                   const char *value,
                                   config_validation_result_t *result)
{
    if (!config || !value)
    {
        return false;
    }

    config_validation_result_t validation;
    config_validation_result_t *validation_ptr = result ? result : &validation;

    // Validate the value
    if (!config_schema_validate_field(field_id, value, validation_ptr))
    {
        return false;
    }

    // Set the value based on field type
    switch (field_id)
    {
    // String fields
    case CONFIG_FIELD_WIFI_SSID:
        strncpy(config->wifi_ssid, value, sizeof(config->wifi_ssid) - 1);
        break;
    case CONFIG_FIELD_WIFI_PASSWORD:
        strncpy(config->wifi_password, value, sizeof(config->wifi_password) - 1);
        break;
    case CONFIG_FIELD_WIFI_STATIC_IP:
        strncpy(config->wifi_static_ip, value, sizeof(config->wifi_static_ip) - 1);
        break;
    case CONFIG_FIELD_WIFI_GATEWAY:
        strncpy(config->wifi_gateway, value, sizeof(config->wifi_gateway) - 1);
        break;
    case CONFIG_FIELD_WIFI_SUBNET:
        strncpy(config->wifi_subnet, value, sizeof(config->wifi_subnet) - 1);
        break;
    case CONFIG_FIELD_WIFI_DNS_PRIMARY:
        strncpy(config->wifi_dns_primary, value, sizeof(config->wifi_dns_primary) - 1);
        break;
    case CONFIG_FIELD_WIFI_DNS_SECONDARY:
        strncpy(config->wifi_dns_secondary, value, sizeof(config->wifi_dns_secondary) - 1);
        break;
    case CONFIG_FIELD_TCP_SERVER_IP:
        strncpy(config->tcp_server_ip, value, sizeof(config->tcp_server_ip) - 1);
        break;
    case CONFIG_FIELD_UDP_SERVER_IP:
        strncpy(config->udp_server_ip, value, sizeof(config->udp_server_ip) - 1);
        break;
    case CONFIG_FIELD_UDP_MULTICAST_GROUP:
        strncpy(config->udp_multicast_group, value, sizeof(config->udp_multicast_group) - 1);
        break;
    case CONFIG_FIELD_AUTH_USERNAME:
        strncpy(config->auth_username, value, sizeof(config->auth_username) - 1);
        break;
    case CONFIG_FIELD_AUTH_PASSWORD:
        strncpy(config->auth_password, value, sizeof(config->auth_password) - 1);
        break;
    case CONFIG_FIELD_NTP_SERVER:
        strncpy(config->ntp_server, value, sizeof(config->ntp_server) - 1);
        break;
    case CONFIG_FIELD_NTP_TIMEZONE:
        strncpy(config->ntp_timezone, value, sizeof(config->ntp_timezone) - 1);
        break;

    // Numeric fields
    case CONFIG_FIELD_WIFI_USE_STATIC_IP:
        config->wifi_use_static_ip = (strcmp(value, "1") == 0 || strcmp(value, "true") == 0);
        break;
    case CONFIG_FIELD_TCP_SERVER_PORT:
        config->tcp_server_port = (uint16_t)strtoul(value, NULL, 10);
        break;
    case CONFIG_FIELD_UDP_SERVER_PORT:
        config->udp_server_port = (uint16_t)strtoul(value, NULL, 10);
        break;
    case CONFIG_FIELD_UDP_MULTICAST_PORT:
        config->udp_multicast_port = (uint16_t)strtoul(value, NULL, 10);
        break;
    case CONFIG_FIELD_STREAMING_PROTOCOL:
        config->streaming_protocol = (uint8_t)strtoul(value, NULL, 10);
        break;
    case CONFIG_FIELD_AUDIO_SAMPLE_RATE:
        config->audio_sample_rate = strtoul(value, NULL, 10);
        break;
    case CONFIG_FIELD_AUDIO_BITS_PER_SAMPLE:
        config->audio_bits_per_sample = (uint8_t)strtoul(value, NULL, 10);
        break;
    case CONFIG_FIELD_AUDIO_CHANNELS:
        config->audio_channels = (uint8_t)strtoul(value, NULL, 10);
        break;
    case CONFIG_FIELD_AUDIO_BCK_PIN:
        config->audio_bck_pin = (uint8_t)strtoul(value, NULL, 10);
        break;
    case CONFIG_FIELD_AUDIO_WS_PIN:
        config->audio_ws_pin = (uint8_t)strtoul(value, NULL, 10);
        break;
    case CONFIG_FIELD_AUDIO_DATA_IN_PIN:
        config->audio_data_in_pin = (uint8_t)strtoul(value, NULL, 10);
        break;

    // Buffer fields
    case CONFIG_FIELD_BUFFER_RING_SIZE:
        config->buffer_ring_size = strtoul(value, NULL, 10);
        break;
    case CONFIG_FIELD_BUFFER_DMA_COUNT:
        config->buffer_dma_count = (uint8_t)strtoul(value, NULL, 10);
        break;
    case CONFIG_FIELD_BUFFER_DMA_LENGTH:
        config->buffer_dma_length = (uint16_t)strtoul(value, NULL, 10);
        break;
    case CONFIG_FIELD_BUFFER_ADAPTIVE_ENABLED:
        config->buffer_adaptive_enabled = (strcmp(value, "1") == 0 || strcmp(value, "true") == 0);
        break;
    case CONFIG_FIELD_BUFFER_ADAPTIVE_MIN_SIZE:
        config->buffer_adaptive_min_size = strtoul(value, NULL, 10);
        break;
    case CONFIG_FIELD_BUFFER_ADAPTIVE_MAX_SIZE:
        config->buffer_adaptive_max_size = strtoul(value, NULL, 10);
        break;
    case CONFIG_FIELD_BUFFER_ADAPTIVE_THRESHOLD_LOW:
        config->buffer_adaptive_threshold_low = (uint8_t)strtoul(value, NULL, 10);
        break;
    case CONFIG_FIELD_BUFFER_ADAPTIVE_THRESHOLD_HIGH:
        config->buffer_adaptive_threshold_high = (uint8_t)strtoul(value, NULL, 10);
        break;
    case CONFIG_FIELD_BUFFER_ADAPTIVE_CHECK_INTERVAL:
        config->buffer_adaptive_check_interval = strtoul(value, NULL, 10);
        break;
    case CONFIG_FIELD_BUFFER_ADAPTIVE_RESIZE_DELAY:
        config->buffer_adaptive_resize_delay = strtoul(value, NULL, 10);
        break;

    // Continue with other fields...
    case CONFIG_FIELD_TASK_I2S_PRIORITY:
        config->task_i2s_priority = (uint8_t)strtoul(value, NULL, 10);
        break;
    case CONFIG_FIELD_TASK_TCP_PRIORITY:
        config->task_tcp_priority = (uint8_t)strtoul(value, NULL, 10);
        break;
    case CONFIG_FIELD_TASK_WATCHDOG_PRIORITY:
        config->task_watchdog_priority = (uint8_t)strtoul(value, NULL, 10);
        break;
    case CONFIG_FIELD_TASK_WEB_PRIORITY:
        config->task_web_priority = (uint8_t)strtoul(value, NULL, 10);
        break;
    case CONFIG_FIELD_TASK_I2S_CORE:
        config->task_i2s_core = (uint8_t)strtoul(value, NULL, 10);
        break;
    case CONFIG_FIELD_TASK_TCP_CORE:
        config->task_tcp_core = (uint8_t)strtoul(value, NULL, 10);
        break;
    case CONFIG_FIELD_TASK_WATCHDOG_CORE:
        config->task_watchdog_core = (uint8_t)strtoul(value, NULL, 10);
        break;
    case CONFIG_FIELD_TASK_WEB_CORE:
        config->task_web_core = (uint8_t)strtoul(value, NULL, 10);
        break;

    // Error handling fields
    case CONFIG_FIELD_ERROR_MAX_RECONNECT_ATTEMPTS:
        config->error_max_reconnect_attempts = (uint16_t)strtoul(value, NULL, 10);
        break;
    case CONFIG_FIELD_ERROR_RECONNECT_BACKOFF_MS:
        config->error_reconnect_backoff_ms = strtoul(value, NULL, 10);
        break;
    case CONFIG_FIELD_ERROR_MAX_RECONNECT_BACKOFF_MS:
        config->error_max_reconnect_backoff_ms = strtoul(value, NULL, 10);
        break;
    case CONFIG_FIELD_ERROR_MAX_I2S_FAILURES:
        config->error_max_i2s_failures = (uint16_t)strtoul(value, NULL, 10);
        break;
    case CONFIG_FIELD_ERROR_MAX_BUFFER_OVERFLOWS:
        config->error_max_buffer_overflows = (uint16_t)strtoul(value, NULL, 10);
        break;
    case CONFIG_FIELD_ERROR_WATCHDOG_TIMEOUT_SEC:
        config->error_watchdog_timeout_sec = (uint16_t)strtoul(value, NULL, 10);
        break;
    case CONFIG_FIELD_ERROR_NTP_RESYNC_INTERVAL_SEC:
        config->error_ntp_resync_interval_sec = strtoul(value, NULL, 10);
        break;

    // Debug fields
    case CONFIG_FIELD_DEBUG_ENABLED:
        config->debug_enabled = (strcmp(value, "1") == 0 || strcmp(value, "true") == 0);
        break;
    case CONFIG_FIELD_DEBUG_STACK_MONITORING:
        config->debug_stack_monitoring = (strcmp(value, "1") == 0 || strcmp(value, "true") == 0);
        break;
    case CONFIG_FIELD_DEBUG_AUTO_REBOOT:
        config->debug_auto_reboot = (strcmp(value, "1") == 0 || strcmp(value, "true") == 0);
        break;
    case CONFIG_FIELD_DEBUG_I2S_REINIT:
        config->debug_i2s_reinit = (strcmp(value, "1") == 0 || strcmp(value, "true") == 0);
        break;
    case CONFIG_FIELD_DEBUG_BUFFER_DRAIN:
        config->debug_buffer_drain = (strcmp(value, "1") == 0 || strcmp(value, "true") == 0);
        break;

    // UDP fields
    case CONFIG_FIELD_UDP_PACKET_MAX_SIZE:
        config->udp_packet_max_size = strtoul(value, NULL, 10);
        break;
    case CONFIG_FIELD_UDP_SEND_TIMEOUT_MS:
        config->udp_send_timeout_ms = strtoul(value, NULL, 10);
        break;
    case CONFIG_FIELD_UDP_BUFFER_COUNT:
        config->udp_buffer_count = (uint8_t)strtoul(value, NULL, 10);
        break;

    // TCP optimization fields
    case CONFIG_FIELD_TCP_KEEPALIVE_ENABLED:
        config->tcp_keepalive_enabled = (strcmp(value, "1") == 0 || strcmp(value, "true") == 0);
        break;
    case CONFIG_FIELD_TCP_KEEPALIVE_IDLE_SEC:
        config->tcp_keepalive_idle_sec = strtoul(value, NULL, 10);
        break;
    case CONFIG_FIELD_TCP_KEEPALIVE_INTERVAL_SEC:
        config->tcp_keepalive_interval_sec = strtoul(value, NULL, 10);
        break;
    case CONFIG_FIELD_TCP_KEEPALIVE_COUNT:
        config->tcp_keepalive_count = strtoul(value, NULL, 10);
        break;
    case CONFIG_FIELD_TCP_NODELAY_ENABLED:
        config->tcp_nodelay_enabled = (strcmp(value, "1") == 0 || strcmp(value, "true") == 0);
        break;
    case CONFIG_FIELD_TCP_TX_BUFFER_SIZE:
        config->tcp_tx_buffer_size = strtoul(value, NULL, 10);
        break;
    case CONFIG_FIELD_TCP_RX_BUFFER_SIZE:
        config->tcp_rx_buffer_size = strtoul(value, NULL, 10);
        break;

    // Performance monitoring fields
    case CONFIG_FIELD_PERF_INTERVAL_MS:
        config->perf_interval_ms = strtoul(value, NULL, 10);
        break;
    case CONFIG_FIELD_PERF_MAX_ENTRIES:
        config->perf_max_entries = strtoul(value, NULL, 10);
        break;

    default:
        return false;
    }

    return true;
}

bool config_schema_get_field_value(const unified_config_t *config,
                                   config_field_id_t field_id,
                                   char *buffer,
                                   size_t buffer_size)
{
    if (!config || !buffer || buffer_size == 0)
    {
        return false;
    }

    switch (field_id)
    {
    // String fields
    case CONFIG_FIELD_WIFI_SSID:
        strncpy(buffer, config->wifi_ssid, buffer_size - 1);
        break;
    case CONFIG_FIELD_WIFI_PASSWORD:
        strncpy(buffer, config->wifi_password, buffer_size - 1); // Return actual password for connection
        break;
    case CONFIG_FIELD_WIFI_STATIC_IP:
        strncpy(buffer, config->wifi_static_ip, buffer_size - 1);
        break;
    case CONFIG_FIELD_WIFI_GATEWAY:
        strncpy(buffer, config->wifi_gateway, buffer_size - 1);
        break;
    case CONFIG_FIELD_WIFI_SUBNET:
        strncpy(buffer, config->wifi_subnet, buffer_size - 1);
        break;
    case CONFIG_FIELD_WIFI_DNS_PRIMARY:
        strncpy(buffer, config->wifi_dns_primary, buffer_size - 1);
        break;
    case CONFIG_FIELD_WIFI_DNS_SECONDARY:
        strncpy(buffer, config->wifi_dns_secondary, buffer_size - 1);
        break;
    case CONFIG_FIELD_TCP_SERVER_IP:
        strncpy(buffer, config->tcp_server_ip, buffer_size - 1);
        break;
    case CONFIG_FIELD_UDP_SERVER_IP:
        strncpy(buffer, config->udp_server_ip, buffer_size - 1);
        break;
    case CONFIG_FIELD_UDP_MULTICAST_GROUP:
        strncpy(buffer, config->udp_multicast_group, buffer_size - 1);
        break;
    case CONFIG_FIELD_AUTH_USERNAME:
        strncpy(buffer, config->auth_username, buffer_size - 1);
        break;
    case CONFIG_FIELD_AUTH_PASSWORD:
        strncpy(buffer, config->auth_password, buffer_size - 1); // Return actual password for auth checks
        break;
    case CONFIG_FIELD_NTP_SERVER:
        strncpy(buffer, config->ntp_server, buffer_size - 1);
        break;
    case CONFIG_FIELD_NTP_TIMEZONE:
        strncpy(buffer, config->ntp_timezone, buffer_size - 1);
        break;

    // Numeric fields
    case CONFIG_FIELD_WIFI_USE_STATIC_IP:
        snprintf(buffer, buffer_size, "%d", config->wifi_use_static_ip ? 1 : 0);
        break;
    case CONFIG_FIELD_TCP_SERVER_PORT:
        snprintf(buffer, buffer_size, "%d", config->tcp_server_port);
        break;
    case CONFIG_FIELD_UDP_SERVER_PORT:
        snprintf(buffer, buffer_size, "%d", config->udp_server_port);
        break;
    case CONFIG_FIELD_UDP_MULTICAST_PORT:
        snprintf(buffer, buffer_size, "%d", config->udp_multicast_port);
        break;
    case CONFIG_FIELD_STREAMING_PROTOCOL:
        snprintf(buffer, buffer_size, "%d", config->streaming_protocol);
        break;
    case CONFIG_FIELD_AUDIO_SAMPLE_RATE:
        snprintf(buffer, buffer_size, "%lu", (unsigned long)config->audio_sample_rate);
        break;
    case CONFIG_FIELD_AUDIO_BITS_PER_SAMPLE:
        snprintf(buffer, buffer_size, "%d", config->audio_bits_per_sample);
        break;
    case CONFIG_FIELD_AUDIO_CHANNELS:
        snprintf(buffer, buffer_size, "%d", config->audio_channels);
        break;
    case CONFIG_FIELD_AUDIO_BCK_PIN:
        snprintf(buffer, buffer_size, "%d", config->audio_bck_pin);
        break;
    case CONFIG_FIELD_AUDIO_WS_PIN:
        snprintf(buffer, buffer_size, "%d", config->audio_ws_pin);
        break;
    case CONFIG_FIELD_AUDIO_DATA_IN_PIN:
        snprintf(buffer, buffer_size, "%d", config->audio_data_in_pin);
        break;

    // Buffer fields
    case CONFIG_FIELD_BUFFER_RING_SIZE:
        snprintf(buffer, buffer_size, "%lu", (unsigned long)config->buffer_ring_size);
        break;
    case CONFIG_FIELD_BUFFER_DMA_COUNT:
        snprintf(buffer, buffer_size, "%d", config->buffer_dma_count);
        break;
    case CONFIG_FIELD_BUFFER_DMA_LENGTH:
        snprintf(buffer, buffer_size, "%d", config->buffer_dma_length);
        break;
    case CONFIG_FIELD_BUFFER_ADAPTIVE_ENABLED:
        snprintf(buffer, buffer_size, "%d", config->buffer_adaptive_enabled ? 1 : 0);
        break;
    case CONFIG_FIELD_BUFFER_ADAPTIVE_MIN_SIZE:
        snprintf(buffer, buffer_size, "%lu", (unsigned long)config->buffer_adaptive_min_size);
        break;
    case CONFIG_FIELD_BUFFER_ADAPTIVE_MAX_SIZE:
        snprintf(buffer, buffer_size, "%lu", (unsigned long)config->buffer_adaptive_max_size);
        break;
    case CONFIG_FIELD_BUFFER_ADAPTIVE_THRESHOLD_LOW:
        snprintf(buffer, buffer_size, "%d", config->buffer_adaptive_threshold_low);
        break;
    case CONFIG_FIELD_BUFFER_ADAPTIVE_THRESHOLD_HIGH:
        snprintf(buffer, buffer_size, "%d", config->buffer_adaptive_threshold_high);
        break;
    case CONFIG_FIELD_BUFFER_ADAPTIVE_CHECK_INTERVAL:
        snprintf(buffer, buffer_size, "%lu", (unsigned long)config->buffer_adaptive_check_interval);
        break;
    case CONFIG_FIELD_BUFFER_ADAPTIVE_RESIZE_DELAY:
        snprintf(buffer, buffer_size, "%lu", (unsigned long)config->buffer_adaptive_resize_delay);
        break;

    // Continue with other fields...
    case CONFIG_FIELD_TASK_I2S_PRIORITY:
        snprintf(buffer, buffer_size, "%d", config->task_i2s_priority);
        break;
    case CONFIG_FIELD_TASK_TCP_PRIORITY:
        snprintf(buffer, buffer_size, "%d", config->task_tcp_priority);
        break;
    case CONFIG_FIELD_TASK_WATCHDOG_PRIORITY:
        snprintf(buffer, buffer_size, "%d", config->task_watchdog_priority);
        break;
    case CONFIG_FIELD_TASK_WEB_PRIORITY:
        snprintf(buffer, buffer_size, "%d", config->task_web_priority);
        break;
    case CONFIG_FIELD_TASK_I2S_CORE:
        snprintf(buffer, buffer_size, "%d", config->task_i2s_core);
        break;
    case CONFIG_FIELD_TASK_TCP_CORE:
        snprintf(buffer, buffer_size, "%d", config->task_tcp_core);
        break;
    case CONFIG_FIELD_TASK_WATCHDOG_CORE:
        snprintf(buffer, buffer_size, "%d", config->task_watchdog_core);
        break;
    case CONFIG_FIELD_TASK_WEB_CORE:
        snprintf(buffer, buffer_size, "%d", config->task_web_core);
        break;

    // Error handling fields
    case CONFIG_FIELD_ERROR_MAX_RECONNECT_ATTEMPTS:
        snprintf(buffer, buffer_size, "%d", config->error_max_reconnect_attempts);
        break;
    case CONFIG_FIELD_ERROR_RECONNECT_BACKOFF_MS:
        snprintf(buffer, buffer_size, "%lu", (unsigned long)config->error_reconnect_backoff_ms);
        break;
    case CONFIG_FIELD_ERROR_MAX_RECONNECT_BACKOFF_MS:
        snprintf(buffer, buffer_size, "%lu", (unsigned long)config->error_max_reconnect_backoff_ms);
        break;
    case CONFIG_FIELD_ERROR_MAX_I2S_FAILURES:
        snprintf(buffer, buffer_size, "%d", config->error_max_i2s_failures);
        break;
    case CONFIG_FIELD_ERROR_MAX_BUFFER_OVERFLOWS:
        snprintf(buffer, buffer_size, "%d", config->error_max_buffer_overflows);
        break;
    case CONFIG_FIELD_ERROR_WATCHDOG_TIMEOUT_SEC:
        snprintf(buffer, buffer_size, "%d", config->error_watchdog_timeout_sec);
        break;
    case CONFIG_FIELD_ERROR_NTP_RESYNC_INTERVAL_SEC:
        snprintf(buffer, buffer_size, "%lu", (unsigned long)config->error_ntp_resync_interval_sec);
        break;

    // Debug fields
    case CONFIG_FIELD_DEBUG_ENABLED:
        snprintf(buffer, buffer_size, "%d", config->debug_enabled ? 1 : 0);
        break;
    case CONFIG_FIELD_DEBUG_STACK_MONITORING:
        snprintf(buffer, buffer_size, "%d", config->debug_stack_monitoring ? 1 : 0);
        break;
    case CONFIG_FIELD_DEBUG_AUTO_REBOOT:
        snprintf(buffer, buffer_size, "%d", config->debug_auto_reboot ? 1 : 0);
        break;
    case CONFIG_FIELD_DEBUG_I2S_REINIT:
        snprintf(buffer, buffer_size, "%d", config->debug_i2s_reinit ? 1 : 0);
        break;
    case CONFIG_FIELD_DEBUG_BUFFER_DRAIN:
        snprintf(buffer, buffer_size, "%d", config->debug_buffer_drain ? 1 : 0);
        break;

    // UDP fields
    case CONFIG_FIELD_UDP_PACKET_MAX_SIZE:
        snprintf(buffer, buffer_size, "%lu", (unsigned long)config->udp_packet_max_size);
        break;
    case CONFIG_FIELD_UDP_SEND_TIMEOUT_MS:
        snprintf(buffer, buffer_size, "%lu", (unsigned long)config->udp_send_timeout_ms);
        break;
    case CONFIG_FIELD_UDP_BUFFER_COUNT:
        snprintf(buffer, buffer_size, "%d", config->udp_buffer_count);
        break;

    // TCP optimization fields
    case CONFIG_FIELD_TCP_KEEPALIVE_ENABLED:
        snprintf(buffer, buffer_size, "%d", config->tcp_keepalive_enabled ? 1 : 0);
        break;
    case CONFIG_FIELD_TCP_KEEPALIVE_IDLE_SEC:
        snprintf(buffer, buffer_size, "%lu", (unsigned long)config->tcp_keepalive_idle_sec);
        break;
    case CONFIG_FIELD_TCP_KEEPALIVE_INTERVAL_SEC:
        snprintf(buffer, buffer_size, "%lu", (unsigned long)config->tcp_keepalive_interval_sec);
        break;
    case CONFIG_FIELD_TCP_KEEPALIVE_COUNT:
        snprintf(buffer, buffer_size, "%lu", (unsigned long)config->tcp_keepalive_count);
        break;
    case CONFIG_FIELD_TCP_NODELAY_ENABLED:
        snprintf(buffer, buffer_size, "%d", config->tcp_nodelay_enabled ? 1 : 0);
        break;
    case CONFIG_FIELD_TCP_TX_BUFFER_SIZE:
        snprintf(buffer, buffer_size, "%lu", (unsigned long)config->tcp_tx_buffer_size);
        break;
    case CONFIG_FIELD_TCP_RX_BUFFER_SIZE:
        snprintf(buffer, buffer_size, "%lu", (unsigned long)config->tcp_rx_buffer_size);
        break;

    // Performance monitoring fields
    case CONFIG_FIELD_PERF_INTERVAL_MS:
        snprintf(buffer, buffer_size, "%lu", (unsigned long)config->perf_interval_ms);
        break;
    case CONFIG_FIELD_PERF_MAX_ENTRIES:
        snprintf(buffer, buffer_size, "%zu", config->perf_max_entries);
        break;

    default:
        return false;
    }

    buffer[buffer_size - 1] = '\0';
    return true;
}

bool config_schema_convert_legacy(const void *legacy_config, unified_config_t *unified_config)
{
    if (!legacy_config || !unified_config)
    {
        return false;
    }

    ESP_LOGI("CONFIG_SCHEMA", "Converting legacy configuration to unified format");

    // Initialize with defaults first
    config_schema_init_defaults(unified_config);

    // For now, we implement a basic migration that attempts to read from the old NVS namespace
    // This allows existing deployments to migrate seamlessly to the new unified system

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("audio_config", NVS_READONLY, &nvs_handle);
    if (err == ESP_OK)
    {
        ESP_LOGI("CONFIG_SCHEMA", "Found legacy configuration, migrating to unified format");

        // WiFi configuration migration
        size_t required_size = 0;
        err = nvs_get_str(nvs_handle, "wifi_ssid", NULL, &required_size);
        if (err == ESP_OK && required_size > 0)
        {
            char *ssid = (char *)malloc(required_size);
            if (ssid && nvs_get_str(nvs_handle, "wifi_ssid", ssid, &required_size) == ESP_OK)
            {
                strncpy(unified_config->wifi_ssid, ssid, sizeof(unified_config->wifi_ssid) - 1);
            }
            free(ssid);
        }

        required_size = 0;
        err = nvs_get_str(nvs_handle, "wifi_pass", NULL, &required_size);
        if (err == ESP_OK && required_size > 0)
        {
            char *password = (char *)malloc(required_size);
            if (password && nvs_get_str(nvs_handle, "wifi_pass", password, &required_size) == ESP_OK)
            {
                strncpy(unified_config->wifi_password, password, sizeof(unified_config->wifi_password) - 1);
            }
            free(password);
        }

        // TCP server configuration migration
        required_size = 0;
        err = nvs_get_str(nvs_handle, "tcp_ip", NULL, &required_size);
        if (err == ESP_OK && required_size > 0)
        {
            char *tcp_ip = (char *)malloc(required_size);
            if (tcp_ip && nvs_get_str(nvs_handle, "tcp_ip", tcp_ip, &required_size) == ESP_OK)
            {
                strncpy(unified_config->tcp_server_ip, tcp_ip, sizeof(unified_config->tcp_server_ip) - 1);
                strncpy(unified_config->udp_server_ip, tcp_ip, sizeof(unified_config->udp_server_ip) - 1); // Use same for UDP
            }
            free(tcp_ip);
        }

        uint16_t tcp_port = 0;
        err = nvs_get_u16(nvs_handle, "tcp_port", &tcp_port);
        if (err == ESP_OK)
        {
            unified_config->tcp_server_port = tcp_port;
            unified_config->udp_server_port = tcp_port + 1; // Default UDP port
        }

        // GPIO pin configuration migration
        uint8_t gpio_val = 0;
        err = nvs_get_u8(nvs_handle, "i2s_bck", &gpio_val);
        if (err == ESP_OK)
        {
            unified_config->audio_bck_pin = gpio_val;
        }

        err = nvs_get_u8(nvs_handle, "i2s_ws", &gpio_val);
        if (err == ESP_OK)
        {
            unified_config->audio_ws_pin = gpio_val;
        }

        err = nvs_get_u8(nvs_handle, "i2s_data", &gpio_val);
        if (err == ESP_OK)
        {
            unified_config->audio_data_in_pin = gpio_val;
        }

        // Buffer configuration migration
        uint32_t buffer_size = 0;
        err = nvs_get_u32(nvs_handle, "buffer_size", &buffer_size);
        if (err == ESP_OK)
        {
            unified_config->buffer_ring_size = buffer_size;
        }

        // Task configuration migration
        uint8_t priority_val = 0;
        err = nvs_get_u8(nvs_handle, "i2s_prio", &priority_val);
        if (err == ESP_OK)
        {
            unified_config->task_i2s_priority = priority_val;
        }

        err = nvs_get_u8(nvs_handle, "tcp_prio", &priority_val);
        if (err == ESP_OK)
        {
            unified_config->task_tcp_priority = priority_val;
        }

        err = nvs_get_u8(nvs_handle, "watchdog_prio", &priority_val);
        if (err == ESP_OK)
        {
            unified_config->task_watchdog_priority = priority_val;
        }

        // Core assignments
        uint8_t core_val = 0;
        err = nvs_get_u8(nvs_handle, "i2s_core", &core_val);
        if (err == ESP_OK)
        {
            unified_config->task_i2s_core = core_val;
        }

        err = nvs_get_u8(nvs_handle, "tcp_core", &core_val);
        if (err == ESP_OK)
        {
            unified_config->task_tcp_core = core_val;
        }

        err = nvs_get_u8(nvs_handle, "watchdog_core", &core_val);
        if (err == ESP_OK)
        {
            unified_config->task_watchdog_core = core_val;
        }

        // Error handling configuration
        uint16_t error_val = 0;
        err = nvs_get_u16(nvs_handle, "max_reconnect", &error_val);
        if (err == ESP_OK)
        {
            unified_config->error_max_reconnect_attempts = error_val;
        }

        err = nvs_get_u16(nvs_handle, "max_i2s_fail", &error_val);
        if (err == ESP_OK)
        {
            unified_config->error_max_i2s_failures = error_val;
        }

        err = nvs_get_u16(nvs_handle, "max_overflow", &error_val);
        if (err == ESP_OK)
        {
            unified_config->error_max_buffer_overflows = error_val;
        }

        uint32_t timeout_val = 0;
        err = nvs_get_u32(nvs_handle, "watchdog_timeout", &timeout_val);
        if (err == ESP_OK)
        {
            unified_config->error_watchdog_timeout_sec = timeout_val;
        }

        nvs_close(nvs_handle);
        ESP_LOGI("CONFIG_SCHEMA", "Legacy configuration migration completed successfully");
        return true;
    }
    else
    {
        ESP_LOGI("CONFIG_SCHEMA", "No legacy configuration found, using defaults");
        // No legacy config found, stick with defaults
        return true;
    }
}