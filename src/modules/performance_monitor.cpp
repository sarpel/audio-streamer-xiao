#include "../config.h"
#include "performance_monitor.h"
#include "tcp_streamer.h"
#include "udp_streamer.h"
#include "buffer_manager.h"
#include "network_manager.h"
#include "config_manager.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "PERFORMANCE_MONITOR";

// Monitoring state
static bool monitoring_enabled = true;
static uint32_t monitoring_interval_ms = HISTORY_INTERVAL_MS;
static uint32_t last_collection_time = 0;

// Historical data storage
static performance_metrics_t history[MAX_HISTORY_ENTRIES];
static size_t history_head = 0;
static size_t history_count = 0;
static SemaphoreHandle_t history_mutex = NULL;

// Alert storage
static performance_alert_t alerts[MAX_ALERTS];
static size_t alert_head = 0;
static size_t alert_count = 0;
static SemaphoreHandle_t alert_mutex = NULL;

// Statistics
static uint32_t total_drops = 0;
static uint32_t max_buffer_usage = 0;
static uint64_t total_buffer_usage = 0;
static uint32_t uptime_samples = 0;

// Forward declarations
static void performance_monitor_task(void *arg);
static void check_and_generate_alerts(const performance_metrics_t *metrics);
static TaskHandle_t monitor_task_handle = NULL;

bool performance_monitor_init(void)
{
    ESP_LOGI(TAG, "Initializing performance monitoring system");

    // Create mutexes
    history_mutex = xSemaphoreCreateMutex();
    if (history_mutex == NULL)
    {
        ESP_LOGE(TAG, "Failed to create history mutex");
        return false;
    }

    alert_mutex = xSemaphoreCreateMutex();
    if (alert_mutex == NULL)
    {
        ESP_LOGE(TAG, "Failed to create alert mutex");
        vSemaphoreDelete(history_mutex);
        return false;
    }

    // Initialize storage
    memset(history, 0, sizeof(history));
    memset(alerts, 0, sizeof(alerts));
    history_head = 0;
    history_count = 0;
    alert_head = 0;
    alert_count = 0;

    // Reset statistics
    total_drops = 0;
    max_buffer_usage = 0;
    total_buffer_usage = 0;
    uptime_samples = 0;
    last_collection_time = 0;

    // Create monitoring task
    BaseType_t result = xTaskCreate(
        performance_monitor_task,
        "perf_monitor",
        4096,
        NULL,
        5, // Low priority
        &monitor_task_handle);

    if (result != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create monitoring task");
        vSemaphoreDelete(history_mutex);
        vSemaphoreDelete(alert_mutex);
        return false;
    }

    ESP_LOGI(TAG, "Performance monitoring initialized");
    return true;
}

void performance_monitor_deinit(void)
{
    monitoring_enabled = false;

    if (monitor_task_handle != NULL)
    {
        vTaskDelete(monitor_task_handle);
        monitor_task_handle = NULL;
    }

    if (history_mutex != NULL)
    {
        vSemaphoreDelete(history_mutex);
        history_mutex = NULL;
    }

    if (alert_mutex != NULL)
    {
        vSemaphoreDelete(alert_mutex);
        alert_mutex = NULL;
    }

    ESP_LOGI(TAG, "Performance monitoring deinitialized");
}

performance_metrics_t performance_monitor_collect_metrics(void)
{
    performance_metrics_t metrics;
    // Initialize all fields explicitly to avoid missing initializer warnings
    metrics.tcp_bytes_sent = 0;
    metrics.tcp_reconnects = 0;
    metrics.tcp_connected = false;
    metrics.udp_bytes_sent = 0;
    metrics.udp_packets_sent = 0;
    metrics.udp_lost_packets = 0;
    metrics.udp_connected = false;
    metrics.udp_packet_loss_rate = 0.0;
    metrics.buffer_usage_percent = 0;
    metrics.buffer_available_samples = 0;
    metrics.buffer_free_space_samples = 0;
    metrics.buffer_overflow_detected = false;
    metrics.free_heap = 0;
    metrics.min_free_heap = 0;
    metrics.largest_free_block = 0;
    metrics.fragmentation_percent = 0.0;
    metrics.uptime_sec = 0;
    metrics.wifi_rssi = 0;
    metrics.wifi_channel = 0;
    metrics.audio_data_rate_bps = 0;
    metrics.timestamp_ms = esp_timer_get_time() / 1000;

// Network metrics
#if STREAMING_PROTOCOL == STREAMING_PROTOCOL_TCP || STREAMING_PROTOCOL == STREAMING_PROTOCOL_BOTH
    tcp_streamer_get_stats(&metrics.tcp_bytes_sent, &metrics.tcp_reconnects);
    metrics.tcp_connected = tcp_streamer_is_connected();
#endif

#if STREAMING_PROTOCOL == STREAMING_PROTOCOL_UDP || STREAMING_PROTOCOL == STREAMING_PROTOCOL_BOTH
    uint64_t udp_bytes_sent = 0;
    udp_streamer_get_stats(&udp_bytes_sent, &metrics.udp_packets_sent, &metrics.udp_lost_packets);
    metrics.udp_bytes_sent = udp_bytes_sent;
    metrics.udp_connected = udp_streamer_is_connected();
    metrics.udp_packet_loss_rate = (metrics.udp_packets_sent > 0) ? (double)metrics.udp_lost_packets / metrics.udp_packets_sent * 100.0 : 0.0;
#endif

    // Buffer metrics
    metrics.buffer_usage_percent = buffer_manager_usage_percent();
    metrics.buffer_available_samples = buffer_manager_available();
    metrics.buffer_free_space_samples = buffer_manager_free_space();
    metrics.buffer_overflow_detected = buffer_manager_check_overflow();

#if ADAPTIVE_BUFFERING_ENABLED
    size_t current_buffer_size = 0;
    buffer_manager_adaptive_get_stats(&current_buffer_size, NULL, NULL);
    // Store buffer size info in available_samples for now
    metrics.buffer_available_samples = current_buffer_size;
#endif

    // Memory metrics
    metrics.free_heap = esp_get_free_heap_size();
    metrics.min_free_heap = esp_get_minimum_free_heap_size();
    metrics.largest_free_block = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    metrics.fragmentation_percent = (metrics.largest_free_block > 0) ? (1.0 - (double)metrics.largest_free_block / metrics.free_heap) * 100.0 : 0.0;

    // System metrics
    metrics.uptime_sec = esp_timer_get_time() / 1000000;

    // WiFi metrics
    if (network_manager_is_connected())
    {
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK)
        {
            metrics.wifi_rssi = ap_info.rssi;
            metrics.wifi_channel = ap_info.primary;
        }
    }

    // Audio metrics (fixed 16kHz 16-bit mono)
    // Note: audio_data_rate_bps field name implies bits per second, so we multiply by 8 for bits
    metrics.audio_data_rate_bps = SAMPLE_RATE * CHANNELS * BITS_PER_SAMPLE;

    return metrics;
}

size_t performance_monitor_get_history(uint32_t start_timestamp_ms, uint32_t end_timestamp_ms,
                                       performance_metrics_t *metrics, size_t max_entries)
{
    if (!metrics || max_entries == 0)
    {
        return 0;
    }

    if (xSemaphoreTake(history_mutex, pdMS_TO_TICKS(1000)) != pdTRUE)
    {
        ESP_LOGW(TAG, "Failed to acquire history mutex");
        return 0;
    }

    size_t count = 0;
    uint32_t now = esp_timer_get_time() / 1000;

    if (end_timestamp_ms == 0)
    {
        end_timestamp_ms = now;
    }

    // Search history for entries in the time range
    for (size_t i = 0; i < history_count && count < max_entries; i++)
    {
        size_t index = (history_head - 1 - i + MAX_HISTORY_ENTRIES) % MAX_HISTORY_ENTRIES;
        const performance_metrics_t *entry = &history[index];

        if (entry->timestamp_ms >= start_timestamp_ms && entry->timestamp_ms <= end_timestamp_ms)
        {
            metrics[count] = *entry;
            count++;
        }
        else if (entry->timestamp_ms < start_timestamp_ms)
        {
            break; // History is ordered, we can stop
        }
    }

    xSemaphoreGive(history_mutex);
    return count;
}

bool performance_monitor_get_latest(performance_metrics_t *metrics)
{
    if (!metrics)
    {
        return false;
    }

    if (xSemaphoreTake(history_mutex, pdMS_TO_TICKS(1000)) != pdTRUE)
    {
        ESP_LOGW(TAG, "Failed to acquire history mutex");
        return false;
    }

    bool result = false;
    if (history_count > 0)
    {
        size_t latest_index = (history_head - 1 + MAX_HISTORY_ENTRIES) % MAX_HISTORY_ENTRIES;
        *metrics = history[latest_index];
        result = true;
    }

    xSemaphoreGive(history_mutex);
    return result;
}

void performance_monitor_add_alert(alert_level_t level, const char *category, const char *message)
{
    if (!category || !message)
    {
        return;
    }

    if (xSemaphoreTake(alert_mutex, pdMS_TO_TICKS(100)) != pdTRUE)
    {
        ESP_LOGW(TAG, "Failed to acquire alert mutex");
        return;
    }

    // Add new alert
    performance_alert_t *alert = &alerts[alert_head];
    alert->timestamp_ms = esp_timer_get_time() / 1000;
    alert->level = level;
    strncpy(alert->category, category, sizeof(alert->category) - 1);
    strncpy(alert->message, message, sizeof(alert->message) - 1);
    alert->category[sizeof(alert->category) - 1] = '\0';
    alert->message[sizeof(alert->message) - 1] = '\0';

    // Update head and count
    alert_head = (alert_head + 1) % MAX_ALERTS;
    if (alert_count < MAX_ALERTS)
    {
        alert_count++;
    }

    xSemaphoreGive(alert_mutex);

    // Log the alert
    const char *level_str = "INFO";
    switch (level)
    {
    case ALERT_LEVEL_WARNING:
        level_str = "WARNING";
        break;
    case ALERT_LEVEL_ERROR:
        level_str = "ERROR";
        break;
    case ALERT_LEVEL_CRITICAL:
        level_str = "CRITICAL";
        break;
    default:
        break;
    }

    ESP_LOGW(TAG, "ALERT [%s] %s: %s", level_str, category, message);
}

size_t performance_monitor_get_alerts(performance_alert_t *alerts_out, size_t max_entries,
                                      alert_level_t level)
{
    if (!alerts_out || max_entries == 0)
    {
        return 0;
    }

    if (xSemaphoreTake(alert_mutex, pdMS_TO_TICKS(1000)) != pdTRUE)
    {
        ESP_LOGW(TAG, "Failed to acquire alert mutex");
        return 0;
    }

    size_t count = 0;

    // Search alerts for entries matching the level filter
    for (size_t i = 0; i < alert_count && count < max_entries; i++)
    {
        size_t index = (alert_head - 1 - i + MAX_ALERTS) % MAX_ALERTS;
        const performance_alert_t *alert = &alerts[index];

        if (alert->level >= level)
        {
            alerts_out[count] = *alert;
            count++;
        }
    }

    xSemaphoreGive(alert_mutex);
    return count;
}

void performance_monitor_clear_alerts(void)
{
    if (xSemaphoreTake(alert_mutex, pdMS_TO_TICKS(1000)) != pdTRUE)
    {
        ESP_LOGW(TAG, "Failed to acquire alert mutex");
        return;
    }

    alert_head = 0;
    alert_count = 0;
    memset(alerts, 0, sizeof(alerts));

    xSemaphoreGive(alert_mutex);
    ESP_LOGI(TAG, "Alerts cleared");
}

void performance_monitor_get_summary(uint32_t duration_min,
                                     uint32_t *avg_buffer_usage,
                                     uint32_t *max_buffer_usage_out,
                                     uint32_t *total_drops_out,
                                     uint32_t *uptime_percent)
{
    if (avg_buffer_usage)
        *avg_buffer_usage = 0;
    if (max_buffer_usage_out)
        *max_buffer_usage_out = max_buffer_usage;
    if (total_drops_out)
        *total_drops_out = total_drops;
    if (uptime_percent)
        *uptime_percent = 100; // Always up for now

    if (uptime_samples > 0 && avg_buffer_usage)
    {
        *avg_buffer_usage = total_buffer_usage / uptime_samples;
    }
}

void performance_monitor_set_enabled(bool enabled)
{
    monitoring_enabled = enabled;
    ESP_LOGI(TAG, "Performance monitoring %s", enabled ? "enabled" : "disabled");
}

bool performance_monitor_is_enabled(void)
{
    return monitoring_enabled;
}

void performance_monitor_set_interval(uint32_t interval_ms)
{
    monitoring_interval_ms = interval_ms;
    ESP_LOGI(TAG, "Monitoring interval set to %lu ms", interval_ms);
}

uint32_t performance_monitor_get_interval(void)
{
    return monitoring_interval_ms;
}

// Static helper functions
static void performance_monitor_task(void *arg)
{
    ESP_LOGI(TAG, "Performance monitoring task started");

    while (1)
    {
        if (monitoring_enabled)
        {
            uint32_t current_time = esp_timer_get_time() / 1000;

            if (current_time - last_collection_time >= monitoring_interval_ms)
            {
                // Collect metrics
                performance_metrics_t metrics = performance_monitor_collect_metrics();

                // Store in history
                if (xSemaphoreTake(history_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
                {
                    history[history_head] = metrics;
                    history_head = (history_head + 1) % MAX_HISTORY_ENTRIES;
                    if (history_count < MAX_HISTORY_ENTRIES)
                    {
                        history_count++;
                    }

                    // Update statistics
                    total_buffer_usage += metrics.buffer_usage_percent;
                    uptime_samples++;
                    if (metrics.buffer_usage_percent > max_buffer_usage)
                    {
                        max_buffer_usage = metrics.buffer_usage_percent;
                    }

                    if (metrics.buffer_overflow_detected)
                    {
                        total_drops++;
                    }

                    xSemaphoreGive(history_mutex);
                }

                // Check for alerts
                check_and_generate_alerts(&metrics);

                last_collection_time = current_time;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1000)); // Check every second
    }
}

static void check_and_generate_alerts(const performance_metrics_t *metrics)
{
    // Buffer overflow alert
    if (metrics->buffer_overflow_detected)
    {
        performance_monitor_add_alert(ALERT_LEVEL_WARNING, "buffer", "Buffer overflow detected");
    }

    // High buffer usage alert
    if (metrics->buffer_usage_percent > 90)
    {
        performance_monitor_add_alert(ALERT_LEVEL_WARNING, "buffer", "Buffer usage > 90%");
    }

    // Low memory alert
    if (metrics->free_heap < 20480) // Less than 20KB
    {
        performance_monitor_add_alert(ALERT_LEVEL_ERROR, "memory", "Low heap memory");
    }

    // High fragmentation alert
    if (metrics->fragmentation_percent > 50)
    {
        performance_monitor_add_alert(ALERT_LEVEL_WARNING, "memory", "High memory fragmentation");
    }

    // WiFi signal strength alert
    if (metrics->wifi_rssi < -80 && metrics->wifi_rssi != 0)
    {
        performance_monitor_add_alert(ALERT_LEVEL_WARNING, "wifi", "Weak WiFi signal");
    }

// Network connectivity alert
#if STREAMING_PROTOCOL == STREAMING_PROTOCOL_TCP || STREAMING_PROTOCOL == STREAMING_PROTOCOL_BOTH
    if (!metrics->tcp_connected)
    {
        performance_monitor_add_alert(ALERT_LEVEL_ERROR, "network", "TCP disconnected");
    }
#endif

#if STREAMING_PROTOCOL == STREAMING_PROTOCOL_UDP || STREAMING_PROTOCOL == STREAMING_PROTOCOL_BOTH
    if (!metrics->udp_connected)
    {
        performance_monitor_add_alert(ALERT_LEVEL_ERROR, "network", "UDP disconnected");
    }

    if (metrics->udp_packet_loss_rate > 10.0)
    {
        performance_monitor_add_alert(ALERT_LEVEL_WARNING, "network", "High UDP packet loss");
    }
#endif
}