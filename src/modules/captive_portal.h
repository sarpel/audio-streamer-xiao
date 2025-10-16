#ifndef CAPTIVE_PORTAL_H
#define CAPTIVE_PORTAL_H

#include <stdbool.h>

/**
 * Initialize captive portal in AP mode
 * This starts a WiFi access point and DNS server for first-time setup
 * @return true on success
 */
bool captive_portal_init(void);

/**
 * Check if captive portal is active
 * @return true if running in portal mode
 */
bool captive_portal_is_active(void);

/**
 * Stop captive portal and switch to normal WiFi mode
 */
void captive_portal_stop(void);

/**
 * Check if initial configuration has been completed
 * @return true if WiFi credentials have been configured
 */
bool captive_portal_is_configured(void);

/**
 * Check if new configuration was submitted during this portal session
 * @return true if config was updated since portal started
 */
bool captive_portal_config_updated(void);

/**
 * Clear the config updated flag (called when portal starts)
 */
void captive_portal_clear_update_flag(void);

/**
 * Set the config updated flag (called when config is saved via web API)
 */
void captive_portal_mark_config_updated(void);

#endif // CAPTIVE_PORTAL_H
