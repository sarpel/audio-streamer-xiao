#include "network_manager.h"
#include "../config.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_sntp.h"
// #include "mdns.h"  // TODO: Add mDNS support when available
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include <time.h>
#include <sys/time.h>

static const char* TAG = "NETWORK_MANAGER";
static bool wifi_connected = false;
static bool ntp_synced = false;

static uint32_t wifi_disconnect_count = 0;  // ✅ Add counter
static const uint32_t MAX_DISCONNECT_BEFORE_REBOOT = 20;

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        ESP_LOGI(TAG, "WiFi started, connecting...");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_connected = false;
        wifi_disconnect_count++;  // ✅ Track disconnects
        
        ESP_LOGW(TAG, "WiFi disconnected (count: %lu), attempting reconnect...", 
                 wifi_disconnect_count);
        
        // ✅ Add emergency reboot for persistent WiFi issues
        if (wifi_disconnect_count > MAX_DISCONNECT_BEFORE_REBOOT) {
            ESP_LOGE(TAG, "Too many WiFi disconnects, rebooting...");
            vTaskDelay(pdMS_TO_TICKS(1000));
            esp_restart();
        }
        
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        wifi_connected = true;
        wifi_disconnect_count = 0;  // ✅ Reset on successful connection
    }
}

bool network_manager_init(void) {
    esp_err_t ret;

    // Initialize NVS (required for WiFi)
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS needs erase, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
        
        // ✅ Check if second attempt also failed
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "CRITICAL: NVS init failed twice: %s", esp_err_to_name(ret));
            // Try one more time with factory reset
            nvs_flash_erase();
            ret = nvs_flash_init();
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "CRITICAL: NVS permanently corrupted, rebooting...");
                vTaskDelay(pdMS_TO_TICKS(5000));
                esp_restart();
            }
        }
    }
    ESP_ERROR_CHECK(ret);

    // Initialize TCP/IP stack
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t* sta_netif = esp_netif_create_default_wifi_sta();

    // Configure static IP if defined
    if (strcmp(STATIC_IP_ADDR, "0.0.0.0") != 0) {
        esp_netif_dhcpc_stop(sta_netif);

        esp_netif_ip_info_t ip_info;
        memset(&ip_info, 0, sizeof(esp_netif_ip_info_t));

        ip_info.ip.addr = esp_ip4addr_aton(STATIC_IP_ADDR);
        ip_info.gw.addr = esp_ip4addr_aton(GATEWAY_ADDR);
        ip_info.netmask.addr = esp_ip4addr_aton(SUBNET_MASK);

        ESP_ERROR_CHECK(esp_netif_set_ip_info(sta_netif, &ip_info));
        ESP_LOGI(TAG, "Static IP configured: %s", STATIC_IP_ADDR);

        // Configure DNS servers
        if (strcmp(PRIMARY_DNS, "0.0.0.0") != 0) {
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

    // Initialize WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Register event handlers
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               &wifi_event_handler, NULL));

    // Configure WiFi
    wifi_config_t wifi_config = {};
    strncpy((char*)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid));
    strncpy((char*)wifi_config.sta.password, WIFI_PASSWORD, sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    // Disable power save for consistent streaming
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_LOGI(TAG, "WiFi power save disabled for streaming");

    ESP_ERROR_CHECK(esp_wifi_start());

    // Wait for connection (timeout 10 seconds)
    int retry_count = 0;
    while (!wifi_connected && retry_count < WIFI_CONNECT_MAX_RETRIES) {
        vTaskDelay(pdMS_TO_TICKS(500));
        retry_count++;
    }

    if (!wifi_connected) {
        ESP_LOGE(TAG, "Failed to connect to WiFi");
        return false;
    }

    ESP_LOGI(TAG, "WiFi connected successfully");
    return true;
}

bool network_manager_is_connected(void) {
    return wifi_connected;
}

bool network_manager_reconnect(void) {
    if (wifi_connected) {
        return true;  // Already connected
    }

    ESP_LOGI(TAG, "Attempting to reconnect...");
    esp_err_t ret = esp_wifi_connect();

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Reconnect failed: %s", esp_err_to_name(ret));
        return false;
    }

    // Wait for connection (timeout 5 seconds)
    int retry_count = 0;
    while (!wifi_connected && retry_count < 10) {
        vTaskDelay(pdMS_TO_TICKS(500));
        retry_count++;
    }

    return wifi_connected;
}

static void ntp_sync_callback(struct timeval *tv) {
    ESP_LOGI(TAG, "NTP time synchronized");
    ntp_synced = true;
}

bool network_manager_init_ntp(void) {
    ESP_LOGI(TAG, "Initializing NTP...");

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, NTP_SERVER);
    sntp_set_time_sync_notification_cb(ntp_sync_callback);
    esp_sntp_init();

    // Wait for time sync (timeout 10 seconds)
    int retry_count = 0;
    while (!ntp_synced && retry_count < WIFI_CONNECT_MAX_RETRIES) {
        vTaskDelay(pdMS_TO_TICKS(500));
        retry_count++;
    }

    if (!ntp_synced) {
        ESP_LOGW(TAG, "NTP sync timeout, continuing anyway");
        
        // ✅ Add fallback: Set a default time or retry later
        struct timeval tv = {
            .tv_sec = 1704067200,  // Jan 1, 2024 00:00:00 UTC
            .tv_usec = 0
        };
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

bool network_manager_resync_ntp(void) {
    ESP_LOGI(TAG, "Resyncing NTP...");
    ntp_synced = false;
    esp_sntp_stop();
    esp_sntp_init();

    // Wait for resync (timeout 5 seconds)
    int retry_count = 0;
    while (!ntp_synced && retry_count < 10) {
        vTaskDelay(pdMS_TO_TICKS(500));
        retry_count++;
    }

    return ntp_synced;
}

time_t network_manager_get_time(void) {
    time_t now;
    time(&now);
    return now;
}

bool network_manager_init_mdns(void) {
    ESP_LOGI(TAG, "mDNS initialization skipped (not available in this build)");
    ESP_LOGI(TAG, "Access web UI via device IP address");
    return true;
    
    // mDNS is not implemented in this version
    // For mDNS support, see mdns_support.md in documentation
}

void network_manager_deinit(void) {
    esp_sntp_stop();
    // mdns_free();  // TODO: Enable when mDNS is available
    esp_wifi_stop();
    esp_wifi_deinit();
    ESP_LOGI(TAG, "Network manager deinitialized");
}
