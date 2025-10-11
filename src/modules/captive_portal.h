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

#endif // CAPTIVE_PORTAL_H
