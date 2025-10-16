#include "network_manager.h"
#include "../config.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "esp_task_wdt.h"
// #include "mdns.h"  // TODO: Add mDNS support when available
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include <time.h>
#include <sys/time.h>

static const char *TAG = "NETWORK_MANAGER";
static bool wifi_connected = false;
static bool ntp_synced = false;

static uint32_t wifi_disconnect_count = 0;    // ✅ Add counter
static uint32_t wifi_connection_failures = 0; // ✅ 3-strike counter for initial failures
static bool wifi_trials_paused = false;       // ✅ Flag to pause WiFi during captive portal
static const uint32_t MAX_DISCONNECT_BEFORE_REBOOT = 20;
static const uint32_t MAX_INITIAL_FAILURES = 3; // ✅ 3 strikes before captive portal

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
        ESP_LOGI(TAG, "WiFi started, connecting...");
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        wifi_connected = false;
        wifi_disconnect_count++; // ✅ Track disconnects

        // ✅ Count initial connection failures for 3-strike rule
        if (!wifi_trials_paused && wifi_disconnect_count <= MAX_INITIAL_FAILURES)
        {
            wifi_connection_failures++;
            ESP_LOGW(TAG, "WiFi connection attempt %lu/%lu failed",
                     wifi_connection_failures, MAX_INITIAL_FAILURES);
        }

        ESP_LOGW(TAG, "WiFi disconnected (count: %lu), attempting reconnect...",
                 wifi_disconnect_count);

        // ✅ Add emergency reboot for persistent WiFi issues
        if (wifi_disconnect_count > MAX_DISCONNECT_BEFORE_REBOOT)
        {
            ESP_LOGE(TAG, "Too many WiFi disconnects, rebooting...");
            vTaskDelay(pdMS_TO_TICKS(1000));
            esp_restart();
        }

        // ✅ Only attempt reconnect if trials are not paused
        if (!wifi_trials_paused)
        {
            esp_wifi_connect();
        }
        else
        {
            ESP_LOGI(TAG, "WiFi trials paused during captive portal");
        }
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        wifi_connected = true;
        wifi_disconnect_count = 0;    // ✅ Reset on successful connection
        wifi_connection_failures = 0; // ✅ Reset initial failure counter
    }
}

bool network_manager_init(void)
{
    esp_err_t ret;

    // Initialize NVS (required for WiFi)
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_LOGW(TAG, "NVS needs erase, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();

        // ✅ Check if second attempt also failed
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "CRITICAL: NVS init failed twice: %s", esp_err_to_name(ret));
            // Try one more time with factory reset
            nvs_flash_erase();
            ret = nvs_flash_init();
            if (ret != ESP_OK)
            {
                ESP_LOGE(TAG, "CRITICAL: NVS permanently corrupted, rebooting...");
                vTaskDelay(pdMS_TO_TICKS(5000));
                esp_restart();
            }
        }
    }
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "CRITICAL: NVS init failed: %s", esp_err_to_name(ret));
        return false;
    }

    // Initialize TCP/IP stack
    ret = esp_netif_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "CRITICAL: esp_netif_init failed: %s", esp_err_to_name(ret));
        return false;
    }

    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE)
    {
        ESP_LOGE(TAG, "CRITICAL: esp_event_loop_create_default failed: %s", esp_err_to_name(ret));
        return false;
    }

    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    if (sta_netif == NULL)
    {
        ESP_LOGE(TAG, "CRITICAL: esp_netif_create_default_wifi_sta failed");
        return false;
    }

    // Configure static IP if defined
    if (strcmp(STATIC_IP_ADDR, "0.0.0.0") != 0)
    {
        esp_netif_dhcpc_stop(sta_netif);

        esp_netif_ip_info_t ip_info;
        memset(&ip_info, 0, sizeof(esp_netif_ip_info_t));

        ip_info.ip.addr = esp_ip4addr_aton(STATIC_IP_ADDR);
        ip_info.gw.addr = esp_ip4addr_aton(GATEWAY_ADDR);
        ip_info.netmask.addr = esp_ip4addr_aton(SUBNET_MASK);

        ret = esp_netif_set_ip_info(sta_netif, &ip_info);
        if (ret != ESP_OK)
        {
            ESP_LOGW(TAG, "Failed to set static IP: %s, using DHCP", esp_err_to_name(ret));
        }
        else
        {
            ESP_LOGI(TAG, "Static IP configured: %s", STATIC_IP_ADDR);
        }

        // Configure DNS servers
        if (strcmp(PRIMARY_DNS, "0.0.0.0") != 0)
        {
            esp_netif_dns_info_t dns_info;
            dns_info.ip.u_addr.ip4.addr = esp_ip4addr_aton(PRIMARY_DNS);
            esp_netif_set_dns_info(sta_netif, ESP_NETIF_DNS_MAIN, &dns_info);
            ESP_LOGI(TAG, "Primary DNS configured: %s", PRIMARY_DNS);

            // Configure secondary DNS
            dns_info.ip.u_addr.ip4.addr = esp_ip4addr_aton(SECONDARY_DNS);
            esp_netif_set_dns_info(sta_netif, ESP_NETIF_DNS_BACKUP, &dns_info);
            ESP_LOGI(TAG, "Secondary DNS configured: %s", SECONDARY_DNS);
        }
    }

    // Initialize WiFi with optimized configuration
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

#if NETWORK_OPTIMIZATION_ENABLED
    // Apply network stack optimizations
    cfg.dynamic_tx_buf_num = WIFI_DYNAMIC_TX_BUF_NUM;
    cfg.static_tx_buf_num = WIFI_STATIC_TX_BUF_NUM;
    cfg.dynamic_rx_buf_num = WIFI_DYNAMIC_RX_BUF_NUM;
    cfg.static_rx_buf_num = WIFI_STATIC_RX_BUF_NUM;
    cfg.rx_mgmt_buf_num = WIFI_RX_MGMT_BUF_NUM;
    cfg.rx_ba_win = WIFI_RX_BA_WIN;

    ESP_LOGI(TAG, "WiFi optimization: TX buffers %d/%d, RX buffers %d/%d, MGMT %d, BA_WIN %d",
             cfg.static_tx_buf_num, cfg.dynamic_tx_buf_num,
             cfg.static_rx_buf_num, cfg.dynamic_rx_buf_num,
             cfg.rx_mgmt_buf_num, cfg.rx_ba_win);
#endif

    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "CRITICAL: esp_wifi_init failed: %s", esp_err_to_name(ret));
        return false;
    }

    // Register event handlers
    ret = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                     &wifi_event_handler, NULL);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to register WiFi event handler: %s", esp_err_to_name(ret));
        return false;
    }

    ret = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                     &wifi_event_handler, NULL);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to register IP event handler: %s", esp_err_to_name(ret));
        return false;
    }

    // Configure WiFi
    wifi_config_t wifi_config = {};
    strncpy((char *)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid));
    strncpy((char *)wifi_config.sta.password, WIFI_PASSWORD, sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to set WiFi mode: %s", esp_err_to_name(ret));
        return false;
    }

    ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to set WiFi config: %s", esp_err_to_name(ret));
        return false;
    }

    // Disable power save for consistent streaming
    ret = esp_wifi_set_ps(WIFI_PS_NONE);
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "Failed to disable power save: %s", esp_err_to_name(ret));
    }
    else
    {
        ESP_LOGI(TAG, "WiFi power save disabled for streaming");
    }

    ret = esp_wifi_start();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to start WiFi: %s", esp_err_to_name(ret));
        return false;
    }

    // ✅ NON-BLOCKING: Start WiFi connection in background, don't wait
    ESP_LOGI(TAG, "Starting WiFi connection in background...");
    esp_wifi_connect();

    // Return success immediately - connection will happen asynchronously
    // Event handlers will update wifi_connected flag when connection completes
    return true;
}

bool network_manager_is_connected(void)
{
    return wifi_connected;
}

bool network_manager_reconnect(void)
{
    if (wifi_connected)
    {
        return true; // Already connected
    }

    ESP_LOGI(TAG, "Attempting to reconnect...");
    esp_err_t ret = esp_wifi_connect();

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Reconnect failed: %s", esp_err_to_name(ret));
        return false;
    }

    // Wait for connection (timeout 5 seconds)
    int retry_count = 0;
    while (!wifi_connected && retry_count < 10)
    {
        vTaskDelay(pdMS_TO_TICKS(500));
        retry_count++;
    }

    return wifi_connected;
}

static void ntp_sync_callback(struct timeval *tv)
{
    ESP_LOGI(TAG, "NTP time synchronized");
    ntp_synced = true;
}

bool network_manager_init_ntp(void)
{
    ESP_LOGI(TAG, "Initializing NTP...");

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, NTP_SERVER);
    sntp_set_time_sync_notification_cb(ntp_sync_callback);
    esp_sntp_init();

    // Wait for time sync (timeout 10 seconds)
    // ✅ CRITICAL FIX: Feed watchdog during NTP sync wait to prevent TWDT timeout
    int retry_count = 0;
    while (!ntp_synced && retry_count < WIFI_CONNECT_MAX_RETRIES)
    {
        vTaskDelay(pdMS_TO_TICKS(500));
        retry_count++;

        // Feed watchdog every second (2 iterations)
        if (retry_count % 2 == 0)
        {
            esp_task_wdt_reset();
        }
    }

    if (!ntp_synced)
    {
        ESP_LOGW(TAG, "NTP sync timeout, continuing anyway");

        // ✅ Add fallback: Set a default time or retry later
        struct timeval tv = {
            .tv_sec = 1704067200, // Jan 1, 2024 00:00:00 UTC
            .tv_usec = 0};
        settimeofday(&tv, NULL);
        ESP_LOGW(TAG, "Using fallback time: 2024-01-01");

        return false;
    }

    // Set timezone
    setenv("TZ", NTP_TIMEZONE, 1);
    tzset();

    time_t now;
    time(&now);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);

    ESP_LOGI(TAG, "Current time: %04d-%02d-%02d %02d:%02d:%02d",
             timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

    return true;
}

bool network_manager_resync_ntp(void)
{
    ESP_LOGI(TAG, "Resyncing NTP...");
    ntp_synced = false;
    esp_sntp_stop();
    esp_sntp_init();

    // Wait for resync (timeout 5 seconds)
    int retry_count = 0;
    while (!ntp_synced && retry_count < 10)
    {
        vTaskDelay(pdMS_TO_TICKS(500));
        retry_count++;
    }

    return ntp_synced;
}

time_t network_manager_get_time(void)
{
    time_t now;
    time(&now);
    return now;
}

bool network_manager_init_mdns(void)
{
    ESP_LOGI(TAG, "mDNS initialization skipped (not available in this build)");
    ESP_LOGI(TAG, "Access web UI via device IP address");
    return true;

    // mDNS is not implemented in this version
    // For mDNS support, see mdns_support.md in documentation
}

// ✅ NEW: Check if 3-strike threshold reached for captive portal activation
bool network_manager_should_start_captive_portal(void)
{
    return wifi_connection_failures >= MAX_INITIAL_FAILURES;
}

// ✅ NEW: Pause WiFi trials during captive portal
void network_manager_pause_trials(void)
{
    wifi_trials_paused = true;
    ESP_LOGI(TAG, "WiFi trials paused for captive portal");
}

// ✅ NEW: Resume WiFi trials after captive portal timeout
void network_manager_resume_trials(void)
{
    wifi_trials_paused = false;
    ESP_LOGI(TAG, "WiFi trials resumed after captive portal");

    // Attempt connection if not connected
    if (!wifi_connected)
    {
        ESP_LOGI(TAG, "Attempting WiFi connection after captive portal...");
        esp_wifi_connect();
    }
}

// ✅ NEW: Get current failure count
uint32_t network_manager_get_failure_count(void)
{
    return wifi_connection_failures;
}

// ✅ NEW: Reset failure counter (called after successful captive portal config)
void network_manager_reset_failure_count(void)
{
    wifi_connection_failures = 0;
    ESP_LOGI(TAG, "WiFi failure counter reset");
}

void network_manager_deinit(void)
{
    esp_sntp_stop();
    // mdns_free();  // TODO: Enable when mDNS is available
    esp_wifi_stop();
    esp_wifi_deinit();
    ESP_LOGI(TAG, "Network manager deinitialized");
}
