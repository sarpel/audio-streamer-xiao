#ifndef NETWORK_DIAGNOSTICS_H
#define NETWORK_DIAGNOSTICS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define MAX_WIFI_SCAN_RESULTS 20
#define MAX_PING_COUNT 10

typedef struct {
    char ssid[33];
    int8_t rssi;
    uint8_t channel;
    uint8_t auth_mode;
    uint8_t bssid[6];
} wifi_scan_result_t;

typedef struct {
    bool success;
    uint32_t response_time_ms;
    char message[128];
} ping_result_t;

typedef struct {
    uint32_t packets_sent;
    uint32_t packets_received;
    uint64_t bytes_sent;
    uint64_t bytes_received;
    uint32_t connection_drops;
    uint32_t reconnections;
} network_stats_t;

/**
 * Initialize network diagnostics module
 * @return true on success
 */
bool network_diagnostics_init(void);

/**
 * Scan for available WiFi networks
 * @param results Array to store scan results
 * @param max_results Maximum number of results
 * @return Number of networks found
 */
size_t network_diagnostics_wifi_scan(wifi_scan_result_t* results, size_t max_results);

/**
 * Ping a host
 * @param host Hostname or IP address
 * @param count Number of pings (1-10)
 * @param results Array to store ping results
 * @return Number of successful pings
 */
size_t network_diagnostics_ping(const char* host, uint8_t count, ping_result_t* results);

/**
 * Resolve hostname to IP address
 * @param hostname Hostname to resolve
 * @param ip_str Buffer to store IP address (min 16 bytes)
 * @return true on success
 */
bool network_diagnostics_dns_lookup(const char* hostname, char* ip_str);

/**
 * Get current WiFi connection status
 * @param ssid Buffer for SSID (min 33 bytes)
 * @param ip_str Buffer for IP address (min 16 bytes)
 * @param rssi Pointer to store signal strength
 * @return true if connected
 */
bool network_diagnostics_get_status(char* ssid, char* ip_str, int8_t* rssi);

/**
 * Get network statistics
 * @param stats Pointer to store statistics
 * @return true on success
 */
bool network_diagnostics_get_stats(network_stats_t* stats);

/**
 * Update network statistics counters
 * @param bytes_sent Number of bytes sent
 * @param bytes_received Number of bytes received
 */
void network_diagnostics_update_stats(uint32_t bytes_sent, uint32_t bytes_received);

/**
 * Record a connection drop event
 */
void network_diagnostics_record_drop(void);

/**
 * Record a reconnection event
 */
void network_diagnostics_record_reconnect(void);

/**
 * Convert auth mode to string
 * @param auth_mode WiFi auth mode
 * @return String representation
 */
const char* network_diagnostics_auth_to_string(uint8_t auth_mode);

#endif // NETWORK_DIAGNOSTICS_H
