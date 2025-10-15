#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "nvs_flash.h"

#include "config.h"
#include "modules/i2s_handler.h"
#include "modules/network_manager.h"
#include "modules/tcp_streamer.h"
#include "modules/buffer_manager.h"
#include "modules/config_manager.h"
#include "modules/web_server.h"
#include "modules/captive_portal.h"

static const char *TAG = "MAIN";

// Task handles
static TaskHandle_t i2s_reader_task_handle = NULL;
static TaskHandle_t tcp_sender_task_handle = NULL;
static TaskHandle_t watchdog_task_handle = NULL;

// Watchdog flags
static uint32_t i2s_reader_last_feed = 0;
static uint32_t tcp_sender_last_feed = 0;

// Error counters
static uint32_t consecutive_i2s_failures = 0;
static uint32_t consecutive_tcp_failures = 0;
static uint32_t buffer_overflow_count = 0;
static uint32_t last_overflow_time = 0;

/**
 * I2S Reader Task with Error Recovery
 */
static void i2s_reader_task(void *arg)
{
    ESP_LOGI(TAG, "I2S Reader task started");

    const size_t read_samples = 512;
    int32_t *i2s_buffer = (int32_t *)malloc(read_samples * sizeof(int32_t));

    if (i2s_buffer == NULL)
    {
        ESP_LOGE(TAG, "CRITICAL: Failed to allocate I2S buffer");
        esp_restart(); // Critical failure, reboot
        return;
    }

    while (1)
    {
        size_t bytes_read = 0;

        if (i2s_handler_read(i2s_buffer, read_samples, &bytes_read))
        {
            consecutive_i2s_failures = 0; // Reset failure counter

            size_t samples_read = bytes_read / sizeof(int32_t);
            size_t written = buffer_manager_write(i2s_buffer, samples_read);

            if (written < samples_read)
            {
                ESP_LOGW(TAG, "Ring buffer full, dropped %d samples", samples_read - written);

                // Track buffer overflows
                buffer_overflow_count++;
                last_overflow_time = xTaskGetTickCount();

#if ENABLE_BUFFER_DRAIN
                if (buffer_overflow_count > MAX_BUFFER_OVERFLOWS)
                {
                    ESP_LOGW(TAG, "Too many overflows (%lu), forcing buffer drain",
                             buffer_overflow_count);
                    buffer_manager_reset(); // Emergency drain
                    buffer_overflow_count = 0;
                }
#endif
            }

            i2s_reader_last_feed = xTaskGetTickCount();

            // Log less frequently - every 100 iterations
            static uint32_t log_counter = 0;
            if (++log_counter >= 100)
            {
                uint8_t usage = buffer_manager_usage_percent();
                ESP_LOGI(TAG, "Buffer usage: %d%%", usage);
                log_counter = 0;
            }
        }
        else
        {
            // I2S read failed
            consecutive_i2s_failures++;
            ESP_LOGE(TAG, "I2S read failed (consecutive: %lu)", consecutive_i2s_failures);

#if ENABLE_I2S_REINIT
            if (consecutive_i2s_failures >= MAX_I2S_FAILURES)
            {
                ESP_LOGE(TAG, "Too many I2S failures, reinitializing...");

                // Reinitialize I2S
                i2s_handler_deinit();
                vTaskDelay(pdMS_TO_TICKS(1000));

                if (i2s_handler_init())
                {
                    ESP_LOGI(TAG, "I2S reinitialized successfully");
                    consecutive_i2s_failures = 0;
                }
                else
                {
                    ESP_LOGE(TAG, "I2S reinit failed, rebooting...");
                    esp_restart();
                }
            }
#endif

            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    free(i2s_buffer);
    vTaskDelete(NULL);
}

/**
 * TCP Sender Task with Exponential Backoff
 */
static void tcp_sender_task(void *arg)
{
    ESP_LOGI(TAG, "TCP Sender task started");

    // ✅ FIX: Use stack-allocated buffer instead of heap allocation
    // This avoids OOM crashes from 65KB malloc that was failing
    const size_t send_samples = 4096; // Reduced from 16384 to fit on stack
    static int32_t send_buffer[4096]; // Static allocation in .bss section

    ESP_LOGI(TAG, "Using stack-allocated send buffer (%zu samples)", send_samples);

    ESP_LOGI(TAG, "Waiting for initial buffer fill...");
    vTaskDelay(pdMS_TO_TICKS(5000));

    uint32_t reconnect_backoff_ms = RECONNECT_BACKOFF_MS;
    uint32_t reconnect_attempts = 0;

    while (1)
    {
        size_t min_samples = send_samples / 4;

        // ✅ FIX: Add timeout and periodic yield to prevent watchdog timeout
        uint32_t wait_start = xTaskGetTickCount();
        const uint32_t max_wait_ticks = pdMS_TO_TICKS(2000); // 5 second timeout

        while (buffer_manager_available() < min_samples)
        {
            // ✅ Check if we've been waiting too long
            if ((xTaskGetTickCount() - wait_start) > max_wait_ticks)
            {
                ESP_LOGW(TAG, "Buffer wait timeout, continuing with available data");
                break;
            }

            // ✅ Increase delay to give IDLE task more time
            vTaskDelay(pdMS_TO_TICKS(20)); // Changed from 5ms to 20ms
        }

        size_t samples_read = buffer_manager_read(send_buffer, send_samples);

        if (samples_read > 0)
        {
            if (!tcp_streamer_send_audio(send_buffer, samples_read))
            {
                consecutive_tcp_failures++;
                ESP_LOGE(TAG, "TCP send failed (attempt %lu/%d)",
                         reconnect_attempts, MAX_RECONNECT_ATTEMPTS);

                ESP_LOGI(TAG, "Waiting %lums before reconnect...", reconnect_backoff_ms);
                vTaskDelay(pdMS_TO_TICKS(reconnect_backoff_ms));

                if (tcp_streamer_reconnect())
                {
                    ESP_LOGI(TAG, "TCP reconnected successfully");
                    consecutive_tcp_failures = 0;
                    reconnect_backoff_ms = RECONNECT_BACKOFF_MS;
                    reconnect_attempts = 0;
                }
                else
                {
                    reconnect_attempts++;

                    reconnect_backoff_ms *= 2;
                    if (reconnect_backoff_ms > MAX_RECONNECT_BACKOFF_MS)
                    {
                        reconnect_backoff_ms = MAX_RECONNECT_BACKOFF_MS;
                    }

#if ENABLE_AUTO_REBOOT
                    if (reconnect_attempts >= MAX_RECONNECT_ATTEMPTS)
                    {
                        ESP_LOGE(TAG, "Max reconnect attempts reached, rebooting...");
                        vTaskDelay(pdMS_TO_TICKS(1000));
                        esp_restart();
                    }
#endif
                }
            }
            else
            {
                consecutive_tcp_failures = 0;
                reconnect_backoff_ms = RECONNECT_BACKOFF_MS;
                reconnect_attempts = 0;
            }

            tcp_sender_last_feed = xTaskGetTickCount();
        }
        else
        {
            // ✅ ADD: If no data available, yield and try again
            vTaskDelay(pdMS_TO_TICKS(10));
        }

        if (buffer_manager_check_overflow())
        {
            ESP_LOGW(TAG, "Buffer overflow detected!");
        }

        // ✅ ADD: Explicit yield instead of taskYIELD() macro
        vTaskDelay(pdMS_TO_TICKS(1)); // Small delay to ensure IDLE task runs
    }

    // ✅ FIX: No need to free - using static allocation
    vTaskDelete(NULL);
}

/**
 * Watchdog Task with WiFi Recovery
 */
static void watchdog_task(void *arg)
{
    ESP_LOGI(TAG, "Watchdog task started");

    uint32_t log_counter = 0;
    uint32_t ntp_counter = 0;
    bool wifi_was_connected = true;

    while (1)
    {
        bool wifi_connected = network_manager_is_connected();

        // Detect WiFi state change
        if (!wifi_connected && wifi_was_connected)
        {
            ESP_LOGW(TAG, "WiFi lost, attempting reconnect...");
            network_manager_reconnect();

            // Force TCP reconnect after WiFi recovery
            vTaskDelay(pdMS_TO_TICKS(2000)); // Wait for WiFi to stabilize
            if (network_manager_is_connected())
            {
                ESP_LOGI(TAG, "WiFi recovered, reconnecting TCP...");
                tcp_streamer_reconnect();
            }
        }
        else if (!wifi_connected)
        {
            // Still disconnected, keep trying
            ESP_LOGW(TAG, "WiFi still disconnected");
            network_manager_reconnect();
        }

        wifi_was_connected = wifi_connected;

        // Check task watchdog feeds
        uint32_t now = xTaskGetTickCount();
        uint32_t timeout_ticks = pdMS_TO_TICKS(WATCHDOG_TIMEOUT_SEC * 1000);

        if ((now - i2s_reader_last_feed) > timeout_ticks)
        {
            ESP_LOGE(TAG, "I2S Reader timeout! Last feed: %lu sec ago",
                     (now - i2s_reader_last_feed) / 1000);
#if ENABLE_AUTO_REBOOT
            esp_restart();
#endif
        }

        if ((now - tcp_sender_last_feed) > timeout_ticks)
        {
            ESP_LOGE(TAG, "TCP Sender timeout! Last feed: %lu sec ago",
                     (now - tcp_sender_last_feed) / 1000);
#if ENABLE_AUTO_REBOOT
            esp_restart();
#endif
        }

        // Log statistics every 10 seconds
        if (++log_counter >= 10)
        {
            uint64_t bytes_sent;
            uint32_t reconnects;
            tcp_streamer_get_stats(&bytes_sent, &reconnects);

            ESP_LOGI(TAG, "B:%llu R:%u OF:%lu", bytes_sent, reconnects, buffer_overflow_count);

            // Memory monitoring
            size_t free_heap = esp_get_free_heap_size();
            size_t min_free_heap = esp_get_minimum_free_heap_size();

            if (free_heap < 20480) // Warn if < 20KB free
            {
                ESP_LOGW(TAG, "LOW MEMORY: %zu bytes free (min: %zu)", free_heap, min_free_heap);
            }
            else
            {
                ESP_LOGD(TAG, "Heap: %zu bytes free (min: %zu)", free_heap, min_free_heap);
            }

            // Task CPU usage profiling (requires CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS=y)
#if configGENERATE_RUN_TIME_STATS
            TaskStatus_t task_status[10];
            UBaseType_t task_count = uxTaskGetNumberOfTasks();
            uint32_t total_runtime;

            if (task_count <= 10)
            {
                task_count = uxTaskGetSystemState(task_status, task_count, &total_runtime);

                ESP_LOGI(TAG, "--- Task CPU Usage ---");
                for (UBaseType_t i = 0; i < task_count; i++)
                {
                    uint32_t cpu_percent = 0;
                    if (total_runtime > 0)
                    {
                        cpu_percent = (task_status[i].ulRunTimeCounter * 100UL) / total_runtime;
                    }

                    if (cpu_percent > 5) // Only log tasks using >5% CPU
                    {
                        ESP_LOGI(TAG, "  %s: %lu%% (prio=%u, core=%d)",
                                 task_status[i].pcTaskName,
                                 cpu_percent,
                                 (unsigned int)task_status[i].uxCurrentPriority,
                                 (int)xTaskGetCoreID(task_status[i].xHandle));
                    }
                }
            }
#else
            ESP_LOGD(TAG, "CPU profiling disabled (enable CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS)");
#endif

#if ENABLE_STACK_MONITORING
            // Check stack usage for all tasks
            if (i2s_reader_task_handle != NULL)
            {
                UBaseType_t i2s_stack = uxTaskGetStackHighWaterMark(i2s_reader_task_handle);
                if (i2s_stack < MIN_STACK_WATERMARK)
                {
                    ESP_LOGW(TAG, "⚠️ I2S task low stack: %u bytes free", i2s_stack);
                }
            }

            if (tcp_sender_task_handle != NULL)
            {
                UBaseType_t tcp_stack = uxTaskGetStackHighWaterMark(tcp_sender_task_handle);
                if (tcp_stack < MIN_STACK_WATERMARK)
                {
                    ESP_LOGW(TAG, "⚠️ TCP task low stack: %u bytes free", tcp_stack);
                }
            }

            if (watchdog_task_handle != NULL)
            {
                UBaseType_t wd_stack = uxTaskGetStackHighWaterMark(watchdog_task_handle);
                if (wd_stack < MIN_STACK_WATERMARK / 2)
                { // Lower threshold for watchdog
                    ESP_LOGW(TAG, "⚠️ Watchdog low stack: %u bytes free", wd_stack);
                }
            }
#endif

            log_counter = 0;

            // Reset overflow counter after cooldown
            if ((now - last_overflow_time) > pdMS_TO_TICKS(OVERFLOW_COOLDOWN_MS))
            {
                buffer_overflow_count = 0;
            }
        }

        // NTP resync every hour
        if (++ntp_counter >= NTP_RESYNC_INTERVAL_SEC)
        {
            if (network_manager_is_connected())
            {
                network_manager_resync_ntp();
            }
            ntp_counter = 0;
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    vTaskDelete(NULL);
}

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "=== Audio Streamer Starting ===");
    ESP_LOGI(TAG, "ESP-IDF Version: %s", esp_get_idf_version());

    // ✅ Subscribe app_main to watchdog
    esp_task_wdt_add(xTaskGetCurrentTaskHandle());

    esp_task_wdt_reset();
    // Initialize NVS flash (required for config manager)
    ESP_LOGI(TAG, "Initializing NVS flash...");
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_LOGW(TAG, "NVS partition was truncated, erasing and re-initializing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "CRITICAL: NVS flash init failed: %s, rebooting...", esp_err_to_name(ret));
        vTaskDelay(pdMS_TO_TICKS(5000));
        esp_restart();
    }
    ESP_LOGI(TAG, "NVS flash initialized successfully");

    esp_task_wdt_reset();
    // Initialize configuration manager
    ESP_LOGI(TAG, "Initializing configuration manager...");
    if (!config_manager_init())
    {
        ESP_LOGE(TAG, "CRITICAL: Config manager init failed, rebooting...");
        vTaskDelay(pdMS_TO_TICKS(5000));
        esp_restart();
    }

    // Load configuration from NVS (or defaults on first boot)
    if (!config_manager_load())
    {
        ESP_LOGE(TAG, "CRITICAL: Failed to load configuration, rebooting...");
        vTaskDelay(pdMS_TO_TICKS(5000));
        esp_restart();
    }

    if (config_manager_is_first_boot())
    {
        ESP_LOGI(TAG, "First boot detected - using default configuration");
        config_manager_save(); // Save defaults to NVS
    }

    esp_task_wdt_reset();
    // Check if first boot and try captive portal
    if (config_manager_is_first_boot() || !captive_portal_is_configured())
    {
        ESP_LOGI(TAG, "Starting captive portal for initial setup...");
        if (captive_portal_init())
        {
            ESP_LOGI(TAG, "Captive portal active. Connect to '%s' to configure.", CAPTIVE_PORTAL_SSID);

            // Initialize web server for configuration in AP mode
            if (web_server_init())
            {
                ESP_LOGI(TAG, "Web configuration available at http://192.168.4.1");
            }

            // Wait for configuration (timeout after CAPTIVE_PORTAL_TIMEOUT_SEC)
            uint32_t timeout_ticks = pdMS_TO_TICKS(CAPTIVE_PORTAL_TIMEOUT_SEC * 1000);
            uint32_t start_tick = xTaskGetTickCount();

            while (captive_portal_is_active() &&
                   (xTaskGetTickCount() - start_tick) < timeout_ticks)
            {
                vTaskDelay(pdMS_TO_TICKS(1000));

                // Check if configuration was saved
                if (captive_portal_is_configured())
                {
                    ESP_LOGI(TAG, "Configuration received, stopping captive portal");
                    break;
                }
            }

            captive_portal_stop();
            web_server_deinit();

            if (!captive_portal_is_configured())
            {
                ESP_LOGW(TAG, "Captive portal timeout, continuing with defaults");
            }
            else
            {
                ESP_LOGI(TAG, "Configuration complete, rebooting to apply...");
                vTaskDelay(pdMS_TO_TICKS(2000));
                esp_restart();
            }
        }
    }

    esp_task_wdt_reset();
    // Initialize components with error handling
    ESP_LOGI(TAG, "Initializing WiFi...");
    if (!network_manager_init())
    {
        ESP_LOGE(TAG, "WiFi connection failed, starting captive portal...");

        // Start captive portal for configuration
        if (captive_portal_init())
        {
            ESP_LOGI(TAG, "Captive portal active. Connect to '%s' to configure.", CAPTIVE_PORTAL_SSID);

            // Initialize web server for configuration in AP mode
            if (web_server_init())
            {
                ESP_LOGI(TAG, "Web configuration available at http://192.168.4.1");
            }

            // Wait for configuration (no timeout when triggered by WiFi failure)
            while (captive_portal_is_active())
            {
                vTaskDelay(pdMS_TO_TICKS(1000));

                // Check if configuration was saved
                if (captive_portal_is_configured())
                {
                    ESP_LOGI(TAG, "Configuration received, rebooting to apply...");
                    captive_portal_stop();
                    web_server_deinit();
                    vTaskDelay(pdMS_TO_TICKS(2000));
                    esp_restart();
                }
            }
        }

        // If captive portal failed to start, reboot
        ESP_LOGE(TAG, "CRITICAL: Could not start captive portal, rebooting in 5 seconds...");
        vTaskDelay(pdMS_TO_TICKS(5000));
        esp_restart();
    }

    ESP_LOGI("MAIN", "Free heap: %lu bytes", esp_get_free_heap_size());
    ESP_LOGI("MAIN", "Largest free block: %lu bytes",
             heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

    ESP_LOGI(TAG, "Initializing mDNS...");
    network_manager_init_mdns(); // Non-critical, continue if fails

    ESP_LOGI(TAG, "Initializing NTP...");
    network_manager_init_ntp();

    // Initialize web server
    ESP_LOGI(TAG, "Initializing web server...");
    if (!web_server_init())
    {
        ESP_LOGW(TAG, "Web server init failed, continuing without web UI");
    }
    else
    {
        ESP_LOGI(TAG, "Web UI available at http://audiostreamer.local or device IP");
    }

    ESP_LOGI(TAG, "Initializing ring buffer...");
    if (!buffer_manager_init(RING_BUFFER_SIZE))
    {
        ESP_LOGE(TAG, "CRITICAL: Buffer init failed, rebooting...");
        vTaskDelay(pdMS_TO_TICKS(5000));
        esp_restart();
    }

    esp_task_wdt_reset();
    ESP_LOGI(TAG, "Initializing I2S...");
    if (!i2s_handler_init())
    {
        ESP_LOGE(TAG, "CRITICAL: I2S init failed, rebooting...");
        vTaskDelay(pdMS_TO_TICKS(5000));
        esp_restart();
    }

    esp_task_wdt_reset();
    ESP_LOGI(TAG, "Connecting to TCP server...");
    if (!tcp_streamer_init())
    {
        ESP_LOGW(TAG, "Initial TCP connection failed, will retry in background");
    }

    // Initialize watchdog feed timestamps
    i2s_reader_last_feed = xTaskGetTickCount();
    tcp_sender_last_feed = xTaskGetTickCount();

    // Create tasks
    ESP_LOGI(TAG, "Creating tasks...");

    BaseType_t result;

    esp_task_wdt_reset();
    result = xTaskCreatePinnedToCore(
        i2s_reader_task,
        "I2S_Reader",
        I2S_READER_STACK_SIZE,
        NULL,
        I2S_READER_PRIORITY,
        &i2s_reader_task_handle,
        I2S_READER_CORE);
    if (result != pdPASS)
    {
        ESP_LOGE(TAG, "CRITICAL: Failed to create I2S Reader task");
        esp_restart();
    }
    ESP_LOGI(TAG, "I2S Reader task created");

    esp_task_wdt_reset();
    result = xTaskCreatePinnedToCore(
        tcp_sender_task,
        "TCP_Sender",
        TCP_SENDER_STACK_SIZE,
        NULL,
        TCP_SENDER_PRIORITY,
        &tcp_sender_task_handle,
        TCP_SENDER_CORE);
    if (result != pdPASS)
    {
        ESP_LOGE(TAG, "CRITICAL: Failed to create TCP Sender task");
        esp_restart();
    }
    ESP_LOGI(TAG, "TCP Sender task created");

    esp_task_wdt_reset();
    result = xTaskCreatePinnedToCore(
        watchdog_task,
        "Watchdog",
        WATCHDOG_STACK_SIZE,
        NULL,
        WATCHDOG_PRIORITY,
        &watchdog_task_handle,
        WATCHDOG_CORE);
    if (result != pdPASS)
    {
        ESP_LOGE(TAG, "CRITICAL: Failed to create Watchdog task");
        esp_restart();
    }
    ESP_LOGI(TAG, "Watchdog task created");

    ESP_LOGI(TAG, "=== Audio Streamer Running ===");

    // ✅ Keep app_main alive with watchdog resets
    while (1)
    {
        esp_task_wdt_reset(); // Reset watchdog
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}