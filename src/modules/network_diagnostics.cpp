#include "network_diagnostics.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "lwip/inet.h"
#include "ping/ping_sock.h"
#include <string.h>

static const char* TAG = "NETWORK_DIAG";

// Statistics tracking
static network_stats_t stats;  // Zero-initialized in .bss section (global static)

bool network_diagnostics_init(void) {
    ESP_LOGI(TAG, "Network diagnostics initialized");
    return true;
}

size_t network_diagnostics_wifi_scan(wifi_scan_result_t* results, size_t max_results) {
    if (results == NULL || max_results == 0) {
        return 0;
    }

    // Start WiFi scan
    wifi_scan_config_t scan_config;
    memset(&scan_config, 0, sizeof(scan_config));  // Zero-initialize all fields
    scan_config.ssid = NULL;
    scan_config.bssid = NULL;
    scan_config.channel = 0;
    scan_config.show_hidden = false;
    scan_config.scan_type = WIFI_SCAN_TYPE_ACTIVE;
    scan_config.scan_time.active.min = 100;
    scan_config.scan_time.active.max = 300;

    esp_err_t err = esp_wifi_scan_start(&scan_config, true); // Block until scan complete
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WiFi scan failed: %s", esp_err_to_name(err));
        return 0;
    }

    // Get scan results
    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);

    if (ap_count == 0) {
        ESP_LOGW(TAG, "No WiFi networks found");
        return 0;
    }

    size_t result_count = (ap_count < max_results) ? ap_count : max_results;
    wifi_ap_record_t* ap_records = (wifi_ap_record_t*)malloc(ap_count * sizeof(wifi_ap_record_t));

    if (ap_records == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for scan results");
        return 0;
    }

    err = esp_wifi_scan_get_ap_records(&ap_count, ap_records);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get scan records: %s", esp_err_to_name(err));
        free(ap_records);
        return 0;
    }

    // Copy results
    for (size_t i = 0; i < result_count; i++) {
        strncpy(results[i].ssid, (char*)ap_records[i].ssid, sizeof(results[i].ssid) - 1);
        results[i].ssid[sizeof(results[i].ssid) - 1] = '\0';
        results[i].rssi = ap_records[i].rssi;
        results[i].channel = ap_records[i].primary;
        results[i].auth_mode = ap_records[i].authmode;
        memcpy(results[i].bssid, ap_records[i].bssid, 6);
    }

    free(ap_records);
    ESP_LOGI(TAG, "WiFi scan found %d networks", result_count);

    return result_count;
}

/**
 * Ping success callback - invoked for each successful ping response
 *
 * CRITICAL FIX: ESP-IDF ping library calls this callback for EACH ping in the sequence.
 * The seqno (sequence number) starts at 1 and increments for each ping packet.
 * We MUST index into the results array using (seqno - 1) to avoid memory corruption.
 *
 * Previous buggy code treated 'args' as a single ping_result_t pointer, causing all
 * pings to overwrite results[0]. This fix properly indexes into the array based on seqno.
 */
static void ping_success_cb(esp_ping_handle_t hdl, void *args) {
    ping_result_t* results = (ping_result_t*)args;  // Array of results, not single pointer
    uint8_t ttl;
    uint16_t seqno;
    uint32_t elapsed_time;
    ip_addr_t target_addr;

    esp_ping_get_profile(hdl, ESP_PING_PROF_SEQNO, &seqno, sizeof(seqno));
    esp_ping_get_profile(hdl, ESP_PING_PROF_TTL, &ttl, sizeof(ttl));
    esp_ping_get_profile(hdl, ESP_PING_PROF_TIMEGAP, &elapsed_time, sizeof(elapsed_time));
    esp_ping_get_profile(hdl, ESP_PING_PROF_IPADDR, &target_addr, sizeof(target_addr));

    // Use seqno-1 as index (seqno starts at 1, not 0)
    if (seqno > 0 && seqno <= MAX_PING_COUNT) {
        ping_result_t* result = &results[seqno - 1];  // Correct array indexing
        result->success = true;
        result->response_time_ms = elapsed_time;
        snprintf(result->message, sizeof(result->message),
                 "Reply from " IPSTR ": time=%lums TTL=%d",
                 IP2STR(&target_addr.u_addr.ip4), elapsed_time, ttl);
    }
}

/**
 * Ping timeout callback - invoked when a ping times out
 *
 * Same indexing fix as ping_success_cb: use (seqno - 1) to index into results array.
 */
static void ping_timeout_cb(esp_ping_handle_t hdl, void *args) {
    ping_result_t* results = (ping_result_t*)args;  // Array of results
    uint16_t seqno;

    esp_ping_get_profile(hdl, ESP_PING_PROF_SEQNO, &seqno, sizeof(seqno));

    // Use seqno-1 as index (seqno starts at 1, not 0)
    if (seqno > 0 && seqno <= MAX_PING_COUNT) {
        ping_result_t* result = &results[seqno - 1];  // Correct array indexing
        result->success = false;
        result->response_time_ms = 0;
        snprintf(result->message, sizeof(result->message), "Request timeout for icmp_seq %d", seqno);
    }
}

size_t network_diagnostics_ping(const char* host, uint8_t count, ping_result_t* results) {
    if (host == NULL || results == NULL || count == 0 || count > MAX_PING_COUNT) {
        return 0;
    }

    // Resolve hostname
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));  // Zero-initialize all fields
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_RAW;
    struct addrinfo *res = NULL;

    int err = getaddrinfo(host, NULL, &hints, &res);
    if (err != 0 || res == NULL) {
        ESP_LOGE(TAG, "DNS lookup failed for %s", host);
        return 0;
    }

    struct in_addr addr = ((struct sockaddr_in *)res->ai_addr)->sin_addr;
    freeaddrinfo(res);

    // Configure ping
    esp_ping_config_t ping_config = ESP_PING_DEFAULT_CONFIG();
    ping_config.target_addr.u_addr.ip4.addr = addr.s_addr;
    ping_config.target_addr.type = IPADDR_TYPE_V4;
    ping_config.count = count;
    ping_config.interval_ms = 1000;
    ping_config.timeout_ms = 3000;

    // Ping callbacks
    esp_ping_callbacks_t cbs = {
        .cb_args = results,
        .on_ping_success = ping_success_cb,
        .on_ping_timeout = ping_timeout_cb,
        .on_ping_end = NULL
    };

    esp_ping_handle_t ping_handle;
    err = esp_ping_new_session(&ping_config, &cbs, &ping_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create ping session: %s", esp_err_to_name(err));
        return 0;
    }

    // Start ping
    esp_ping_start(ping_handle);

    // Wait for completion (count * interval + buffer)
    vTaskDelay(pdMS_TO_TICKS((count * 1000) + 2000));

    // Clean up
    esp_ping_stop(ping_handle);
    esp_ping_delete_session(ping_handle);

    // Count successful pings
    size_t success_count = 0;
    for (uint8_t i = 0; i < count; i++) {
        if (results[i].success) {
            success_count++;
        }
    }

    ESP_LOGI(TAG, "Ping to %s: %d/%d successful", host, success_count, count);

    return success_count;
}

bool network_diagnostics_dns_lookup(const char* hostname, char* ip_str) {
    if (hostname == NULL || ip_str == NULL) {
        return false;
    }

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));  // Zero-initialize all fields
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo *res = NULL;

    int err = getaddrinfo(hostname, NULL, &hints, &res);
    if (err != 0 || res == NULL) {
        ESP_LOGE(TAG, "DNS lookup failed for %s: %d", hostname, err);
        return false;
    }

    struct in_addr *addr = &((struct sockaddr_in *)res->ai_addr)->sin_addr;
    inet_ntoa_r(*addr, ip_str, 16);

    freeaddrinfo(res);
    ESP_LOGI(TAG, "DNS lookup: %s -> %s", hostname, ip_str);

    return true;
}

bool network_diagnostics_get_status(char* ssid, char* ip_str, int8_t* rssi) {
    // Get WiFi connection info
    wifi_ap_record_t ap_info;
    esp_err_t err = esp_wifi_sta_get_ap_info(&ap_info);

    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Not connected to WiFi");
        return false;
    }

    if (ssid != NULL) {
        strncpy(ssid, (char*)ap_info.ssid, 33);
        ssid[32] = '\0';
    }

    if (rssi != NULL) {
        *rssi = ap_info.rssi;
    }

    // Get IP address
    if (ip_str != NULL) {
        esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (netif != NULL) {
            esp_netif_ip_info_t ip_info;
            if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
                sprintf(ip_str, IPSTR, IP2STR(&ip_info.ip));
            } else {
                strcpy(ip_str, "0.0.0.0");
            }
        } else {
            strcpy(ip_str, "0.0.0.0");
        }
    }

    return true;
}

bool network_diagnostics_get_stats(network_stats_t* stats_out) {
    if (stats_out == NULL) {
        return false;
    }

    memcpy(stats_out, &stats, sizeof(network_stats_t));
    return true;
}

void network_diagnostics_update_stats(uint32_t bytes_sent, uint32_t bytes_received) {
    stats.packets_sent++;
    stats.packets_received++;
    stats.bytes_sent += bytes_sent;
    stats.bytes_received += bytes_received;
}

void network_diagnostics_record_drop(void) {
    stats.connection_drops++;
}

void network_diagnostics_record_reconnect(void) {
    stats.reconnections++;
}

const char* network_diagnostics_auth_to_string(uint8_t auth_mode) {
    switch(auth_mode) {
        case WIFI_AUTH_OPEN:           return "Open";
        case WIFI_AUTH_WEP:            return "WEP";
        case WIFI_AUTH_WPA_PSK:        return "WPA-PSK";
        case WIFI_AUTH_WPA2_PSK:       return "WPA2-PSK";
        case WIFI_AUTH_WPA_WPA2_PSK:   return "WPA/WPA2-PSK";
        case WIFI_AUTH_WPA3_PSK:       return "WPA3-PSK";
        case WIFI_AUTH_WPA2_WPA3_PSK:  return "WPA2/WPA3-PSK";
        case WIFI_AUTH_WAPI_PSK:       return "WAPI-PSK";
        default:                       return "Unknown";
    }
}
