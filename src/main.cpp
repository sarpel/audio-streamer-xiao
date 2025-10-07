#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_task_wdt.h"

#include "config.h"
#include "modules/i2s_handler.h"
#include "modules/network_manager.h"
#include "modules/tcp_streamer.h"
#include "modules/buffer_manager.h"

static const char* TAG = "MAIN";

// Task handles
static TaskHandle_t i2s_reader_task_handle = NULL;
static TaskHandle_t tcp_sender_task_handle = NULL;
static TaskHandle_t watchdog_task_handle = NULL;

// Watchdog flags
static uint32_t i2s_reader_last_feed = 0;
static uint32_t tcp_sender_last_feed = 0;

/**
 * I2S Reader Task
 *
 * Reads audio samples from I2S DMA buffer and writes to ring buffer.
 * Runs on Core 1 with high priority for real-time audio capture.
 */
static void i2s_reader_task(void* arg) {
    ESP_LOGI(TAG, "I2S Reader task started");

    const size_t read_samples = 512;  // Read 512 samples at a time
    // Note: Allocated for task lifetime, freed automatically on task deletion (never reached in normal operation)
    int32_t* i2s_buffer = (int32_t*)malloc(read_samples * sizeof(int32_t));

    if (i2s_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate I2S buffer");
        vTaskDelete(NULL);
        return;
    }

    while (1) {
        size_t bytes_read = 0;

        // Read from I2S
        if (i2s_handler_read(i2s_buffer, read_samples, &bytes_read)) {
            size_t samples_read = bytes_read / sizeof(int32_t);

            // Write to ring buffer
            size_t written = buffer_manager_write(i2s_buffer, samples_read);

            if (written < samples_read) {
                ESP_LOGW(TAG, "Ring buffer full, dropped %d samples", samples_read - written);
            }

            // Feed watchdog
            i2s_reader_last_feed = xTaskGetTickCount();

            // Log buffer usage periodically
            static uint32_t log_counter = 0;
            if (++log_counter >= 100) {  // Every ~5 seconds at 48kHz
                uint8_t usage = buffer_manager_usage_percent();
                ESP_LOGI(TAG, "Buffer usage: %d%%", usage);
                log_counter = 0;
            }
        } else {
            ESP_LOGE(TAG, "I2S read failed");
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    free(i2s_buffer);
    vTaskDelete(NULL);
}

/**
 * TCP Sender Task
 *
 * Reads samples from ring buffer, packs to 24-bit, and sends via TCP.
 * Runs on Core 1 with lower priority than I2S reader.
 */
static void tcp_sender_task(void* arg) {
    ESP_LOGI(TAG, "TCP Sender task started");

    const size_t send_samples = 9600;  // Send 200ms chunks (optimized from 100ms for lower overhead)
    // Note: Allocated for task lifetime, freed automatically on task deletion (never reached in normal operation)
    int32_t* send_buffer = (int32_t*)malloc(send_samples * sizeof(int32_t));

    if (send_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate send buffer");
        vTaskDelete(NULL);
        return;
    }

    while (1) {
        // Wait for enough samples in buffer
        while (buffer_manager_available() < send_samples) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }

        // Read from ring buffer
        size_t samples_read = buffer_manager_read(send_buffer, send_samples);

        if (samples_read > 0) {
            // Send via TCP
            if (!tcp_streamer_send_audio(send_buffer, samples_read)) {
                ESP_LOGE(TAG, "TCP send failed, attempting reconnect...");

                // Try to reconnect
                if (tcp_streamer_reconnect()) {
                    ESP_LOGI(TAG, "Reconnected successfully");
                } else {
                    ESP_LOGE(TAG, "Reconnect failed, waiting 5 seconds...");
                    vTaskDelay(pdMS_TO_TICKS(5000));
                }
            }

            // Feed watchdog
            tcp_sender_last_feed = xTaskGetTickCount();
        }

        // Check for buffer overflow
        if (buffer_manager_check_overflow()) {
            ESP_LOGW(TAG, "Buffer overflow detected!");
        }
    }

    free(send_buffer);
    vTaskDelete(NULL);
}

/**
 * Watchdog Task
 *
 * Monitors system health and performs periodic maintenance.
 * Runs on Core 0 with low priority.
 */
static void watchdog_task(void* arg) {
    ESP_LOGI(TAG, "Watchdog task started");

    uint32_t ntp_resync_counter = 0;
    const uint32_t ntp_resync_interval = NTP_RESYNC_INTERVAL_SEC;

    while (1) {
        // Check WiFi connection
        if (!network_manager_is_connected()) {
            ESP_LOGW(TAG, "WiFi disconnected, attempting reconnect...");
            network_manager_reconnect();
        }

        // Check TCP connection
        if (!tcp_streamer_is_connected()) {
            ESP_LOGW(TAG, "TCP disconnected");
        }

        // Check task watchdog feeds
        uint32_t now = xTaskGetTickCount();
        uint32_t timeout_ticks = pdMS_TO_TICKS(WATCHDOG_TIMEOUT_SEC * 1000);

        if ((now - i2s_reader_last_feed) > timeout_ticks) {
            ESP_LOGE(TAG, "I2S Reader task timeout! Rebooting...");
            esp_restart();
        }

        if ((now - tcp_sender_last_feed) > timeout_ticks) {
            ESP_LOGE(TAG, "TCP Sender task timeout! Rebooting...");
            esp_restart();
        }

        // Periodic NTP resync
        ntp_resync_counter++;
        if (ntp_resync_counter >= ntp_resync_interval) {
            ESP_LOGI(TAG, "Performing periodic NTP resync...");
            network_manager_resync_ntp();
            ntp_resync_counter = 0;
        }

        // Log statistics
        uint64_t bytes_sent;
        uint32_t reconnects;
        tcp_streamer_get_stats(&bytes_sent, &reconnects);
        ESP_LOGI(TAG, "Stats: %llu bytes sent, %d reconnects", bytes_sent, reconnects);

        uint32_t i2s_overflow, i2s_underflow;
        i2s_handler_get_stats(&i2s_overflow, &i2s_underflow);
        ESP_LOGI(TAG, "I2S: %d overflows, %d underflows", i2s_overflow, i2s_underflow);

        // Sleep for 1 second
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    vTaskDelete(NULL);
}

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "=== Audio Streamer Starting ===");
    ESP_LOGI(TAG, "ESP-IDF Version: %s", esp_get_idf_version());

    // Initialize components
    ESP_LOGI(TAG, "Initializing WiFi...");
    if (!network_manager_init()) {
        ESP_LOGE(TAG, "Failed to initialize WiFi, rebooting...");
        vTaskDelay(pdMS_TO_TICKS(5000));
        esp_restart();
    }

    ESP_LOGI(TAG, "Initializing NTP...");
    network_manager_init_ntp();

    ESP_LOGI(TAG, "Initializing ring buffer...");
    if (!buffer_manager_init(RING_BUFFER_SIZE)) {
        ESP_LOGE(TAG, "Failed to initialize buffer, rebooting...");
        vTaskDelay(pdMS_TO_TICKS(5000));
        esp_restart();
    }

    ESP_LOGI(TAG, "Initializing I2S...");
    if (!i2s_handler_init()) {
        ESP_LOGE(TAG, "Failed to initialize I2S, rebooting...");
        vTaskDelay(pdMS_TO_TICKS(5000));
        esp_restart();
    }

    ESP_LOGI(TAG, "Connecting to TCP server...");
    if (!tcp_streamer_init()) {
        ESP_LOGE(TAG, "Failed to connect to TCP server, rebooting...");
        vTaskDelay(pdMS_TO_TICKS(5000));
        esp_restart();
    }

    // Initialize watchdog feed timestamps
    i2s_reader_last_feed = xTaskGetTickCount();
    tcp_sender_last_feed = xTaskGetTickCount();

    // Create tasks
    ESP_LOGI(TAG, "Creating tasks...");

    xTaskCreatePinnedToCore(
        i2s_reader_task,
        "I2S_Reader",
        I2S_READER_STACK_SIZE,
        NULL,
        I2S_READER_PRIORITY,
        &i2s_reader_task_handle,
        I2S_READER_CORE
    );

    xTaskCreatePinnedToCore(
        tcp_sender_task,
        "TCP_Sender",
        TCP_SENDER_STACK_SIZE,
        NULL,
        TCP_SENDER_PRIORITY,
        &tcp_sender_task_handle,
        TCP_SENDER_CORE
    );

    xTaskCreatePinnedToCore(
        watchdog_task,
        "Watchdog",
        WATCHDOG_STACK_SIZE,
        NULL,
        WATCHDOG_PRIORITY,
        &watchdog_task_handle,
        WATCHDOG_CORE
    );

    ESP_LOGI(TAG, "=== Audio Streamer Running ===");
}
