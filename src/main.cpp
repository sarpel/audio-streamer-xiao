#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_task_wdt.h"
#include "nvs_flash.h"

#include "config.h"
#include "modules/i2s_handler.h"
#include "modules/network_manager.h"
#include "modules/tcp_streamer.h"
#include "modules/udp_streamer.h"
#include "modules/buffer_manager.h"
#include "modules/config_manager.h"
#include "modules/web_server.h"
#include "modules/captive_portal.h"
#include "modules/performance_monitor.h"

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
static uint32_t consecutive_udp_failures = 0;
static uint32_t buffer_overflow_count = 0;
static uint32_t last_overflow_time = 0;

static void start_captive_portal(bool with_timeout);
static void create_tasks(void);

/**
 * I2S Reader Task with Error Recovery
 */
static void i2s_reader_task(void *arg)
{
    ESP_LOGI(TAG, "I2S Reader task started");

    const size_t read_samples = I2S_READ_SAMPLES;
    // ✅ CHANGED: Use int16_t buffer for direct 16-bit I2S reading (50% bandwidth savings)
    int16_t *i2s_buffer = (int16_t *)malloc(read_samples * sizeof(int16_t));
    // ✅ ADD: Temporary buffer for 32-bit samples (required for thread-safe i2s_read_16)
    int32_t *tmp_buffer = (int32_t *)malloc(read_samples * sizeof(int32_t));

    if (i2s_buffer == NULL || tmp_buffer == NULL)
    {
        ESP_LOGE(TAG, "CRITICAL: Failed to allocate I2S buffers");
        free(i2s_buffer);
        free(tmp_buffer);
        esp_restart(); // Critical failure, reboot
        return;
    }

    while (1)
    {
        // ✅ CHANGED: Use i2s_read_16() to read directly as 16-bit samples
        size_t samples_read = i2s_read_16(i2s_buffer, tmp_buffer, read_samples);

        if (samples_read > 0)
        {
            consecutive_i2s_failures = 0; // Reset failure counter

            // ✅ CHANGED: Use buffer_manager_write_16() for native 16-bit storage
            size_t written = buffer_manager_write_16(i2s_buffer, samples_read);

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
                // ✅ Feed watchdog during I2S reinit delay
                for (int j = 0; j < 20; j++) // 20 × 50ms = 1 second
                {
                    esp_task_wdt_reset();
                    vTaskDelay(pdMS_TO_TICKS(50));
                }

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
    free(tmp_buffer);
    vTaskDelete(NULL);
}

/**
 * Network Sender Task with TCP/UDP Support and Exponential Backoff
 */
static void network_sender_task(void *arg)
{
    ESP_LOGI(TAG, "Network Sender task started (TCP/UDP)");

    // ✅ CHANGED: Use int16_t buffer for native 16-bit samples (50% bandwidth savings)
    const size_t send_samples = TCP_SEND_SAMPLES;
    static int16_t send_buffer[TCP_SEND_SAMPLES]; // Static allocation in .bss section

    ESP_LOGI(TAG, "Using 16-bit send buffer (%zu samples)", send_samples);

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

        // ✅ CHANGED: Use buffer_manager_read_16() for native 16-bit samples
        size_t samples_read = buffer_manager_read_16(send_buffer, send_samples);

        if (samples_read > 0)
        {
            bool send_success = false;

// Send data based on configured streaming protocol
#if STREAMING_PROTOCOL == STREAMING_PROTOCOL_TCP
            send_success = tcp_streamer_send_audio_16(send_buffer, samples_read);
#elif STREAMING_PROTOCOL == STREAMING_PROTOCOL_UDP
            send_success = udp_streamer_send_audio_16(send_buffer, samples_read);
#elif STREAMING_PROTOCOL == STREAMING_PROTOCOL_BOTH
            // Send to both TCP and UDP
            bool tcp_success = tcp_streamer_send_audio_16(send_buffer, samples_read);
            bool udp_success = udp_streamer_send_audio_16(send_buffer, samples_read);
            send_success = tcp_success || udp_success; // Consider success if either works
#endif

            if (!send_success)
            {
// Handle connection failures based on active protocol(s)
#if STREAMING_PROTOCOL == STREAMING_PROTOCOL_TCP || STREAMING_PROTOCOL == STREAMING_PROTOCOL_BOTH
                if (!tcp_streamer_is_connected())
                {
                    consecutive_tcp_failures++;
                    ESP_LOGE(TAG, "TCP send failed (attempt %lu/%d)",
                             reconnect_attempts, MAX_RECONNECT_ATTEMPTS);

                    ESP_LOGI(TAG, "Waiting %lums before TCP reconnect...", reconnect_backoff_ms);
                    // ✅ Feed watchdog during TCP reconnect delay (split into smaller chunks)
                    uint32_t delay_chunks = reconnect_backoff_ms / 100;
                    for (int j = 0; j < delay_chunks; j++)
                    {
                        esp_task_wdt_reset();
                        vTaskDelay(pdMS_TO_TICKS(100));
                    }
                    if (reconnect_backoff_ms % 100 != 0)
                    {
                        esp_task_wdt_reset();
                        vTaskDelay(pdMS_TO_TICKS(reconnect_backoff_ms % 100));
                    }

                    if (tcp_streamer_reconnect())
                    {
                        ESP_LOGI(TAG, "TCP reconnected successfully");
                        consecutive_tcp_failures = 0;
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
                            ESP_LOGE(TAG, "Max TCP reconnect attempts reached, rebooting...");
                            // ✅ Feed watchdog during reboot delay
                            for (int j = 0; j < 20; j++) // 20 × 50ms = 1 second
                            {
                                esp_task_wdt_reset();
                                vTaskDelay(pdMS_TO_TICKS(50));
                            }
                            esp_restart();
                        }
#endif
                    }
                }
#endif

#if STREAMING_PROTOCOL == STREAMING_PROTOCOL_UDP || STREAMING_PROTOCOL == STREAMING_PROTOCOL_BOTH
                if (!udp_streamer_is_connected())
                {
                    consecutive_udp_failures++;
                    ESP_LOGE(TAG, "UDP send failed (attempt %lu/%d)",
                             reconnect_attempts, MAX_RECONNECT_ATTEMPTS);

                    ESP_LOGI(TAG, "Waiting %lums before UDP reconnect...", reconnect_backoff_ms);
                    // ✅ Feed watchdog during UDP reconnect delay (split into smaller chunks)
                    uint32_t delay_chunks = reconnect_backoff_ms / 100;
                    for (int j = 0; j < delay_chunks; j++)
                    {
                        esp_task_wdt_reset();
                        vTaskDelay(pdMS_TO_TICKS(100));
                    }
                    if (reconnect_backoff_ms % 100 != 0)
                    {
                        esp_task_wdt_reset();
                        vTaskDelay(pdMS_TO_TICKS(reconnect_backoff_ms % 100));
                    }

                    if (udp_streamer_reconnect())
                    {
                        ESP_LOGI(TAG, "UDP reconnected successfully");
                        consecutive_udp_failures = 0;
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
                            ESP_LOGE(TAG, "Max UDP reconnect attempts reached, rebooting...");
                            // ✅ Feed watchdog during reboot delay
                            for (int j = 0; j < 20; j++) // 20 × 50ms = 1 second
                            {
                                esp_task_wdt_reset();
                                vTaskDelay(pdMS_TO_TICKS(50));
                            }
                            esp_restart();
                        }
#endif
                    }
                }
#endif
            }
            else
            {
                // Reset failure counters on successful send
                consecutive_tcp_failures = 0;
                consecutive_udp_failures = 0;
                reconnect_backoff_ms = RECONNECT_BACKOFF_MS;
                reconnect_attempts = 0;
                tcp_sender_last_feed = xTaskGetTickCount();
            }
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
        // ✅ SKIP watchdog checks during captive portal - let user have time to configure
        if (captive_portal_is_active())
        {
            // Feed the system watchdog periodically but skip timeout checks
            esp_task_wdt_reset();
            vTaskDelay(pdMS_TO_TICKS(5000)); // Check every 5 seconds
            continue;
        }

        bool wifi_connected = network_manager_is_connected();

        // Detect WiFi state change
        if (!wifi_connected && wifi_was_connected)
        {
            ESP_LOGW(TAG, "WiFi lost, attempting reconnect...");
            network_manager_reconnect();

            // Force network reconnect after WiFi recovery
            vTaskDelay(pdMS_TO_TICKS(2000)); // Wait for WiFi to stabilize
            if (network_manager_is_connected())
            {
                ESP_LOGI(TAG, "WiFi recovered, reconnecting network streams...");

#if STREAMING_PROTOCOL == STREAMING_PROTOCOL_TCP || STREAMING_PROTOCOL == STREAMING_PROTOCOL_BOTH
                tcp_streamer_reconnect();
#endif

#if STREAMING_PROTOCOL == STREAMING_PROTOCOL_UDP || STREAMING_PROTOCOL == STREAMING_PROTOCOL_BOTH
                udp_streamer_reconnect();
#endif
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
        if (++log_counter >= WATCHDOG_LOG_INTERVAL_SEC)
        {
            uint64_t tcp_bytes_sent = 0;
            uint32_t tcp_reconnects = 0;

#if STREAMING_PROTOCOL == STREAMING_PROTOCOL_TCP || STREAMING_PROTOCOL == STREAMING_PROTOCOL_BOTH
            tcp_streamer_get_stats(&tcp_bytes_sent, &tcp_reconnects);
#endif

#if STREAMING_PROTOCOL == STREAMING_PROTOCOL_UDP || STREAMING_PROTOCOL == STREAMING_PROTOCOL_BOTH
            udp_streamer_get_stats(&udp_bytes_sent, &udp_packets_sent, &udp_lost);
#endif

// Log protocol-specific stats
#if STREAMING_PROTOCOL == STREAMING_PROTOCOL_TCP
            ESP_LOGI(TAG, "TCP B:%llu R:%u OF:%lu", tcp_bytes_sent, tcp_reconnects, buffer_overflow_count);
#elif STREAMING_PROTOCOL == STREAMING_PROTOCOL_UDP
            uint64_t udp_bytes_sent = 0;
            uint32_t udp_packets_sent = 0, udp_lost = 0;
            udp_streamer_get_stats(&udp_bytes_sent, &udp_packets_sent, &udp_lost);
            ESP_LOGI(TAG, "UDP B:%llu P:%u L:%u OF:%lu", udp_bytes_sent, udp_packets_sent, udp_lost, buffer_overflow_count);
#elif STREAMING_PROTOCOL == STREAMING_PROTOCOL_BOTH
            uint64_t udp_bytes_sent = 0;
            uint32_t udp_packets_sent = 0, udp_lost = 0;
            udp_streamer_get_stats(&udp_bytes_sent, &udp_packets_sent, &udp_lost);
            ESP_LOGI(TAG, "TCP B:%llu R:%u | UDP B:%llu P:%u L:%u OF:%lu",
                     tcp_bytes_sent, tcp_reconnects, udp_bytes_sent, udp_packets_sent, udp_lost, buffer_overflow_count);
#endif

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

#if ADAPTIVE_BUFFERING_ENABLED
        // Check adaptive buffering every ADAPTIVE_CHECK_INTERVAL_MS
        static uint32_t adaptive_counter = 0;
        if (++adaptive_counter >= (ADAPTIVE_CHECK_INTERVAL_MS / 1000))
        {
            buffer_manager_adaptive_check();
            adaptive_counter = 0;
        }
#endif

        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    vTaskDelete(NULL);
}

static void create_tasks(void)
{
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
        network_sender_task,
        "Network_Sender",
        TCP_SENDER_STACK_SIZE,
        NULL,
        TCP_SENDER_PRIORITY,
        &tcp_sender_task_handle,
        TCP_SENDER_CORE);
    if (result != pdPASS)
    {
        ESP_LOGE(TAG, "CRITICAL: Failed to create Network Sender task");
        esp_restart();
    }
    ESP_LOGI(TAG, "Network Sender task created");

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
}

static void start_captive_portal(bool with_timeout)
{
    ESP_LOGI(TAG, "Starting captive portal (5-minute timeout)...");

    // ✅ Pause WiFi trials during captive portal
    network_manager_pause_trials();

    if (captive_portal_init())
    {
        ESP_LOGI(TAG, "Captive portal active. Connect to '%s' to configure.", CAPTIVE_PORTAL_SSID);

        // Initialize web server for configuration in AP mode
        if (web_server_init())
        {
            ESP_LOGI(TAG, "Web configuration v2 available at http://192.168.4.1");
        }

        // ✅ 5-minute timeout with watchdog monitoring
        uint32_t start_tick = xTaskGetTickCount();
        uint32_t timeout_ticks = with_timeout ? pdMS_TO_TICKS(5 * 60 * 1000) : portMAX_DELAY; // 5 minutes

        ESP_LOGI(TAG, "Captive portal will stay active for 5 minutes or until configuration received");

        while (captive_portal_is_active() && (xTaskGetTickCount() - start_tick) < timeout_ticks)
        {
            vTaskDelay(pdMS_TO_TICKS(1000));

            // ✅ CRITICAL: Feed watchdog every iteration to prevent TWDT timeout
            esp_task_wdt_reset();

            uint32_t current_tick = xTaskGetTickCount();

            // ✅ CRITICAL FIX: Check if NEW config was submitted (not just if old config exists)
            if (captive_portal_config_updated())
            {
                ESP_LOGI(TAG, "New configuration received, stopping captive portal");
                break;
            }

            // Log remaining time every 30 seconds
            uint32_t elapsed_sec = (current_tick - start_tick) / 1000;
            uint32_t remaining_sec = 300 - elapsed_sec; // 5 minutes = 300 seconds
            if (remaining_sec % 30 == 0 && remaining_sec > 0)
            {
                ESP_LOGI(TAG, "Captive portal remaining time: %lu seconds", remaining_sec);
            }
        }

        captive_portal_stop();
        web_server_deinit();

        // ✅ CRITICAL FIX: After captive portal closes, check if NEW config was submitted
        if (captive_portal_config_updated())
        {
            ESP_LOGI(TAG, "New configuration submitted, resetting failure counter and attempting WiFi...");
            network_manager_reset_failure_count();
            network_manager_resume_trials();

            // ✅ Give WiFi a chance to connect with new config
            ESP_LOGI(TAG, "Waiting for WiFi connection with new configuration...");
            int wifi_retry = 0;
            while (!network_manager_is_connected() && wifi_retry < 30) // 15 seconds
            {
                vTaskDelay(pdMS_TO_TICKS(500));
                wifi_retry++;
                if (wifi_retry % 4 == 0)
                {
                    esp_task_wdt_reset();
                    ESP_LOGI(TAG, "Connecting to WiFi... (%d/15s)", wifi_retry / 2);
                }
            }

            if (network_manager_is_connected())
            {
                ESP_LOGI(TAG, "WiFi connected successfully with new configuration!");
            }
            else
            {
                ESP_LOGW(TAG, "WiFi connection failed, will retry in background");
            }
        }
        else if (!captive_portal_is_configured() && with_timeout)
        {
            ESP_LOGW(TAG, "Captive portal 5-minute timeout reached without configuration");

            // ✅ Resume WiFi trials after timeout
            network_manager_resume_trials();
            network_manager_reset_failure_count();

            ESP_LOGI(TAG, "WiFi trials resumed - will continue attempting connection forever");
        }
    }
    else
    {
        ESP_LOGE(TAG, "CRITICAL: Could not start captive portal, rebooting in 5 seconds...");
        // ✅ Feed watchdog during delay to prevent timeout
        for (int i = 0; i < 100; i++) // 100 × 50ms = 5 seconds
        {
            esp_task_wdt_reset();
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        esp_restart();
    }
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
        // ✅ Feed watchdog during delay to prevent timeout
        for (int i = 0; i < 100; i++) // 100 × 50ms = 5 seconds
        {
            esp_task_wdt_reset();
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        esp_restart();
    }
    ESP_LOGI(TAG, "NVS flash initialized successfully");

    esp_task_wdt_reset();
    // Initialize configuration manager v2
    ESP_LOGI(TAG, "Initializing configuration manager v2...");
    if (!config_manager_init())
    {
        ESP_LOGE(TAG, "CRITICAL: Config manager v2 init failed, rebooting...");
        // ✅ Feed watchdog during delay to prevent timeout
        for (int i = 0; i < 100; i++) // 100 × 50ms = 5 seconds
        {
            esp_task_wdt_reset();
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        esp_restart();
    }

    // Load configuration from NVS (or defaults on first boot)
    if (!config_manager_load())
    {
        ESP_LOGE(TAG, "CRITICAL: Failed to load configuration, rebooting...");
        // ✅ Feed watchdog during delay to prevent timeout
        for (int i = 0; i < 100; i++) // 100 × 50ms = 5 seconds
        {
            esp_task_wdt_reset();
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        esp_restart();
    }

    if (config_manager_is_first_boot())
    {
        ESP_LOGI(TAG, "First boot detected - using default configuration");
        if (!config_manager_save())
        {
            ESP_LOGE(TAG, "CRITICAL: Failed to save default configuration, rebooting...");
            // ✅ Feed watchdog during delay to prevent timeout
            for (int i = 0; i < 100; i++) // 100 × 50ms = 5 seconds
            {
                esp_task_wdt_reset();
                vTaskDelay(pdMS_TO_TICKS(50));
            }
            esp_restart();
        }
    }

    // ✅ NEW 3-STRIKE APPROACH: Start WiFi and monitor for 3 consecutive failures
    esp_task_wdt_reset();
    ESP_LOGI(TAG, "Initializing WiFi with 3-strike failure detection...");
    if (!network_manager_init())
    {
        ESP_LOGE(TAG, "WiFi initialization failed, starting captive portal immediately");
        start_captive_portal(true);
    }
    else
    {
        // ✅ Monitor WiFi connection attempts for 3-strike rule
        esp_task_wdt_reset();
        ESP_LOGI(TAG, "Monitoring WiFi connection attempts (3-strike rule)...");

        // Give WiFi up to 15 seconds (3 attempts × 5 seconds each)
        int wifi_monitor_count = 0;
        const int max_monitor_cycles = 30; // 30 × 500ms = 15 seconds

        while (wifi_monitor_count < max_monitor_cycles)
        {
            vTaskDelay(pdMS_TO_TICKS(500));
            wifi_monitor_count++;

            // Check if WiFi connected successfully
            if (network_manager_is_connected())
            {
                ESP_LOGI(TAG, "WiFi connected successfully!");

                // Check if this is first boot or needs configuration
                if (config_manager_is_first_boot() || !captive_portal_is_configured())
                {
                    ESP_LOGI(TAG, "Configuration needed, starting captive portal");
                    start_captive_portal(true);
                }
                else
                {
                    ESP_LOGI(TAG, "Device fully configured, continuing normally");
                }
                break;
            }

            // Check if 3-strike threshold reached
            if (network_manager_should_start_captive_portal())
            {
                ESP_LOGW(TAG, "3 WiFi connection failures detected, starting captive portal");
                start_captive_portal(true);
                break;
            }

            // Log progress every 2 seconds
            if (wifi_monitor_count % 4 == 0)
            {
                uint32_t failures = network_manager_get_failure_count();
                ESP_LOGI(TAG, "WiFi monitoring... failures: %lu/3", failures);
            }
        }

        // If we exited monitoring without connection or captive portal, continue anyway
        if (!network_manager_is_connected() && !network_manager_should_start_captive_portal())
        {
            ESP_LOGW(TAG, "WiFi monitoring timeout, continuing without connection");
        }
    }

    ESP_LOGI("MAIN", "Free heap: %lu bytes", esp_get_free_heap_size());
    ESP_LOGI("MAIN", "Largest free block: %lu bytes",
             heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

    ESP_LOGI(TAG, "Initializing mDNS...");
    network_manager_init_mdns(); // Non-critical, continue if fails

    ESP_LOGI(TAG, "Initializing NTP...");
    network_manager_init_ntp();

    // Initialize web server
    ESP_LOGI(TAG, "Initializing web server v2...");
    if (!web_server_init())
    {
        ESP_LOGW(TAG, "Web server v2 init failed, continuing without web UI");
    }
    else
    {
        ESP_LOGI(TAG, "Web UI v2 available at http://audiostreamer.local or device IP");
    }

    ESP_LOGI(TAG, "Initializing ring buffer...");
    size_t buffer_size = RING_BUFFER_SIZE;

#if ADAPTIVE_BUFFERING_ENABLED
    // Use adaptive default size if adaptive buffering is enabled
    buffer_size = ADAPTIVE_BUFFER_DEFAULT_SIZE;
#endif

    if (!buffer_manager_init(buffer_size))
    {
        ESP_LOGE(TAG, "CRITICAL: Buffer init failed, rebooting...");
        // ✅ Feed watchdog during delay to prevent timeout
        for (int i = 0; i < 100; i++) // 100 × 50ms = 5 seconds
        {
            esp_task_wdt_reset();
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        esp_restart();
    }

#if ADAPTIVE_BUFFERING_ENABLED
    // Initialize adaptive buffering system
    if (!buffer_manager_adaptive_init())
    {
        ESP_LOGW(TAG, "Adaptive buffering initialization failed, using static buffer");
    }
    else
    {
        ESP_LOGI(TAG, "Adaptive buffering enabled with %d KB initial size", buffer_size / 1024);
    }
#endif

    // Initialize performance monitoring system
    if (!performance_monitor_init())
    {
        ESP_LOGW(TAG, "Performance monitoring initialization failed");
    }
    else
    {
        ESP_LOGI(TAG, "Performance monitoring initialized");
    }

    esp_task_wdt_reset();
    ESP_LOGI(TAG, "Initializing I2S...");
    if (!i2s_handler_init())
    {
        ESP_LOGE(TAG, "CRITICAL: I2S init failed, rebooting...");
        // ✅ Feed watchdog during delay to prevent timeout
        for (int i = 0; i < 100; i++) // 100 × 50ms = 5 seconds
        {
            esp_task_wdt_reset();
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        esp_restart();
    }

    esp_task_wdt_reset();
    ESP_LOGI(TAG, "Connecting to streaming servers...");

// Initialize TCP streamer if configured
#if STREAMING_PROTOCOL == STREAMING_PROTOCOL_TCP || STREAMING_PROTOCOL == STREAMING_PROTOCOL_BOTH
    if (!tcp_streamer_init())
    {
        ESP_LOGW(TAG, "Initial TCP connection failed, will retry in background");
    }
#endif

// Initialize UDP streamer if configured
#if STREAMING_PROTOCOL == STREAMING_PROTOCOL_UDP || STREAMING_PROTOCOL == STREAMING_PROTOCOL_BOTH
    if (!udp_streamer_init())
    {
        ESP_LOGW(TAG, "Initial UDP initialization failed, will retry in background");
    }
#endif

// Log the active streaming protocol
#if STREAMING_PROTOCOL == STREAMING_PROTOCOL_TCP
    ESP_LOGI(TAG, "TCP streaming enabled");
#elif STREAMING_PROTOCOL == STREAMING_PROTOCOL_UDP
    ESP_LOGI(TAG, "UDP streaming enabled");
#elif STREAMING_PROTOCOL == STREAMING_PROTOCOL_BOTH
    ESP_LOGI(TAG, "TCP and UDP streaming enabled");
#endif

    // Initialize watchdog feed timestamps
    i2s_reader_last_feed = xTaskGetTickCount();
    tcp_sender_last_feed = xTaskGetTickCount();

    create_tasks();

    ESP_LOGI(TAG, "=== Audio Streamer Running ===");

    // ✅ Keep app_main alive with watchdog resets
    while (1)
    {
        esp_task_wdt_reset(); // Reset watchdog
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
