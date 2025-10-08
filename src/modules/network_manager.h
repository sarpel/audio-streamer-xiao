#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include <stdbool.h>
#include <ctime>

/**
 * Initialize WiFi and connect to network
 *
 * Configures WiFi with:
 * - Static IP or DHCP
 * - Power save mode disabled (for consistent streaming)
 * - Auto-reconnect enabled
 *
 * @return true on successful connection, false on failure
 */
bool network_manager_init(void);

/**
 * Check if WiFi is connected
 *
 * @return true if connected, false otherwise
 */
bool network_manager_is_connected(void);

/**
 * Reconnect to WiFi if disconnected
 *
 * @return true on successful reconnection, false on failure
 */
bool network_manager_reconnect(void);

/**
 * Initialize and sync NTP time
 *
 * @return true on successful sync, false on failure
 */
bool network_manager_init_ntp(void);

/**
 * Resync NTP time (call periodically)
 *
 * @return true on successful sync, false on failure
 */
bool network_manager_resync_ntp(void);

/**
 * Get current epoch time from RTC
 *
 * @return Unix timestamp (seconds since epoch)
 */
time_t network_manager_get_time(void);

/**
 * Initialize mDNS service
 * Allows discovery at audiostreamer.local
 * 
 * @return true on success, false on failure
 */
bool network_manager_init_mdns(void);

/**
 * Deinitialize network manager
 */
void network_manager_deinit(void);

#endif // NETWORK_MANAGER_H
