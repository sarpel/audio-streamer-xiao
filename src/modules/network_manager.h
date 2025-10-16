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

/**
 * Check if 3-strike threshold reached for captive portal activation
 *
 * @return true if captive portal should be started, false otherwise
 */
bool network_manager_should_start_captive_portal(void);

/**
 * Pause WiFi trials during captive portal configuration
 */
void network_manager_pause_trials(void);

/**
 * Resume WiFi trials after captive portal timeout
 */
void network_manager_resume_trials(void);

/**
 * Get current WiFi connection failure count
 *
 * @return Number of consecutive connection failures
 */
uint32_t network_manager_get_failure_count(void);

/**
 * Reset WiFi connection failure counter
 * Called after successful captive portal configuration
 */
void network_manager_reset_failure_count(void);

#endif // NETWORK_MANAGER_H
