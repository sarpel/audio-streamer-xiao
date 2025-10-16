#ifndef PERFORMANCE_MONITOR_H
#define PERFORMANCE_MONITOR_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Performance metrics structure
typedef struct {
    uint32_t timestamp_ms;

    // Network metrics
    uint64_t tcp_bytes_sent;
    uint32_t tcp_reconnects;
    bool tcp_connected;
    uint64_t udp_bytes_sent;
    uint32_t udp_packets_sent;
    uint32_t udp_lost_packets;
    bool udp_connected;
    double udp_packet_loss_rate;

    // Buffer metrics
    uint8_t buffer_usage_percent;
    size_t buffer_available_samples;
    size_t buffer_free_space_samples;
    bool buffer_overflow_detected;

    // Memory metrics
    size_t free_heap;
    size_t min_free_heap;
    size_t largest_free_block;
    double fragmentation_percent;

    // System metrics
    uint32_t uptime_sec;
    int8_t wifi_rssi;
    uint8_t wifi_channel;
    uint32_t audio_data_rate_bps;
} performance_metrics_t;

// Alert levels
typedef enum {
    ALERT_LEVEL_INFO = 0,
    ALERT_LEVEL_WARNING = 1,
    ALERT_LEVEL_ERROR = 2,
    ALERT_LEVEL_CRITICAL = 3
} alert_level_t;

// Alert structure
typedef struct {
    uint32_t timestamp_ms;
    alert_level_t level;
    char message[128];
    char category[32];
} performance_alert_t;

// Historical data configuration
#define MAX_HISTORY_ENTRIES 720  // 2 hours of data at 10-second intervals
#define HISTORY_INTERVAL_MS 10000 // Collect data every 10 seconds
// MAX_ALERTS is defined in config.h to avoid redefinition

/**
 * Initialize performance monitoring system
 */
bool performance_monitor_init(void);

/**
 * Deinitialize performance monitoring system
 */
void performance_monitor_deinit(void);

/**
 * Collect current performance metrics
 */
performance_metrics_t performance_monitor_collect_metrics(void);

/**
 * Get historical performance data
 * @param start_timestamp_ms Start time (0 for beginning)
 * @param end_timestamp_ms End time (0 for now)
 * @param metrics Output array for metrics
 * @param max_entries Maximum number of entries to return
 * @return Number of entries returned
 */
size_t performance_monitor_get_history(uint32_t start_timestamp_ms, uint32_t end_timestamp_ms,
                                      performance_metrics_t *metrics, size_t max_entries);

/**
 * Get latest performance metrics
 */
bool performance_monitor_get_latest(performance_metrics_t *metrics);

/**
 * Add an alert
 */
void performance_monitor_add_alert(alert_level_t level, const char *category, const char *message);

/**
 * Get alerts
 * @param alerts Output array for alerts
 * @param max_entries Maximum number of entries to return
 * @param level Minimum alert level to filter (optional, use ALERT_LEVEL_INFO for all)
 * @return Number of entries returned
 */
size_t performance_monitor_get_alerts(performance_alert_t *alerts, size_t max_entries,
                                     alert_level_t level);

/**
 * Clear alerts
 */
void performance_monitor_clear_alerts(void);

/**
 * Get performance statistics summary
 */
void performance_monitor_get_summary(uint32_t duration_min,
                                   uint32_t *avg_buffer_usage,
                                   uint32_t *max_buffer_usage,
                                   uint32_t *total_drops,
                                   uint32_t *uptime_percent);

/**
 * Enable/disable automatic monitoring
 */
void performance_monitor_set_enabled(bool enabled);

/**
 * Check if monitoring is enabled
 */
bool performance_monitor_is_enabled(void);

/**
 * Set monitoring interval
 */
void performance_monitor_set_interval(uint32_t interval_ms);

/**
 * Get monitoring interval
 */
uint32_t performance_monitor_get_interval(void);

#endif // PERFORMANCE_MONITOR_H