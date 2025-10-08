#include "captive_portal.h"
#include "config_manager.h"
#include "../config.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "lwip/dns.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/sockets.h"
#include <string.h>

static const char* TAG = "CAPTIVE_PORTAL";
static bool portal_active = false;
static esp_netif_t *ap_netif = NULL;

// Simple DNS server for captive portal
static void dns_server_task(void *pvParameters) {
    struct sockaddr_in server_addr;
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    
    if (sock < 0) {
        ESP_LOGE(TAG, "Failed to create DNS socket");
        vTaskDelete(NULL);
        return;
    }
    
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(53);
    
    if (bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        ESP_LOGE(TAG, "Failed to bind DNS socket");
        close(sock);
        vTaskDelete(NULL);
        return;
    }
    
    ESP_LOGI(TAG, "DNS server started on port 53");
    
    char rx_buffer[512];
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    
    while (portal_active) {
        int len = recvfrom(sock, rx_buffer, sizeof(rx_buffer) - 1, 0,
                          (struct sockaddr *)&client_addr, &client_addr_len);
        
        if (len > 0 && portal_active) {
            // Simple DNS response - redirect all queries to our IP (192.168.4.1)
            // This is a simplified implementation for captive portal
            uint8_t response[512];
            memcpy(response, rx_buffer, len);
            
            // Set response flags
            response[2] = 0x81; // Response + Recursion available
            response[3] = 0x80; // No error
            
            // Add answer section (point to 192.168.4.1)
            uint8_t answer[] = {
                0xc0, 0x0c,             // Name pointer
                0x00, 0x01,             // Type A
                0x00, 0x01,             // Class IN
                0x00, 0x00, 0x00, 0x3c, // TTL (60 seconds)
                0x00, 0x04,             // Data length
                192, 168, 4, 1          // IP address
            };
            
            memcpy(response + len, answer, sizeof(answer));
            response[7] = 1; // Answer count
            
            sendto(sock, response, len + sizeof(answer), 0,
                  (struct sockaddr *)&client_addr, client_addr_len);
        }
        
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    close(sock);
    ESP_LOGI(TAG, "DNS server stopped");
    vTaskDelete(NULL);
}

bool captive_portal_init(void) {
    if (portal_active) {
        ESP_LOGW(TAG, "Captive portal already active");
        return true;
    }
    
    // Check if already configured
    if (captive_portal_is_configured()) {
        ESP_LOGI(TAG, "Device already configured, skipping captive portal");
        return false;
    }
    
    ESP_LOGI(TAG, "Starting captive portal...");
    
    // Initialize WiFi in AP mode
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK && ret != ESP_ERR_WIFI_INIT_STATE) {
        ESP_LOGE(TAG, "WiFi init failed: %s", esp_err_to_name(ret));
        return false;
    }
    
    // Create AP network interface
    ap_netif = esp_netif_create_default_wifi_ap();
    
    // Configure AP
    wifi_config_t ap_config = {};
    strncpy((char*)ap_config.ap.ssid, CAPTIVE_PORTAL_SSID, sizeof(ap_config.ap.ssid));
    ap_config.ap.ssid_len = strlen(CAPTIVE_PORTAL_SSID);
    ap_config.ap.channel = 1;
    ap_config.ap.authmode = WIFI_AUTH_OPEN;
    ap_config.ap.max_connection = 4;
    
    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    
    // Set IP address for AP
    esp_netif_ip_info_t ip_info;
    IP4_ADDR(&ip_info.ip, 192, 168, 4, 1);
    IP4_ADDR(&ip_info.gw, 192, 168, 4, 1);
    IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);
    
    esp_netif_dhcps_stop(ap_netif);
    esp_netif_set_ip_info(ap_netif, &ip_info);
    esp_netif_dhcps_start(ap_netif);
    
    // Start WiFi
    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi start failed: %s", esp_err_to_name(ret));
        return false;
    }
    
    portal_active = true;
    
    // Start DNS server task
    xTaskCreate(dns_server_task, "dns_server", 4096, NULL, 5, NULL);
    
    ESP_LOGI(TAG, "Captive portal started: SSID=%s, IP=192.168.4.1", CAPTIVE_PORTAL_SSID);
    ESP_LOGI(TAG, "Connect to configure WiFi settings");
    
    return true;
}

bool captive_portal_is_active(void) {
    return portal_active;
}

void captive_portal_stop(void) {
    if (!portal_active) {
        return;
    }
    
    ESP_LOGI(TAG, "Stopping captive portal...");
    portal_active = false;
    
    // Stop WiFi
    esp_wifi_stop();
    
    // Clean up
    if (ap_netif) {
        esp_netif_destroy(ap_netif);
        ap_netif = NULL;
    }
    
    ESP_LOGI(TAG, "Captive portal stopped");
}

bool captive_portal_is_configured(void) {
    wifi_config_data_t wifi;
    if (!config_manager_get_wifi(&wifi)) {
        return false;
    }
    
    // Check if SSID is set and not default
    return (strlen(wifi.ssid) > 0 && strcmp(wifi.ssid, WIFI_SSID) != 0) ||
           config_manager_is_first_boot() == false;
}
