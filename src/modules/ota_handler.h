#ifndef OTA_HANDLER_H
#define OTA_HANDLER_H

#include <stdbool.h>
#include "esp_http_server.h"

/**
 * Initialize OTA handler
 * @return true on success
 */
bool ota_handler_init(void);

/**
 * Register OTA endpoint with web server
 * @param server HTTP server handle
 * @return true on success
 */
bool ota_handler_register_endpoints(httpd_handle_t server);

/**
 * Check if OTA update is in progress
 * @return true if OTA is active
 */
bool ota_handler_is_active(void);

/**
 * Get OTA progress percentage (0-100)
 * @return progress percentage
 */
int ota_handler_get_progress(void);

/**
 * Deinitialize OTA handler
 */
void ota_handler_deinit(void);

#endif // OTA_HANDLER_H
