#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <stdbool.h>
#include "esp_http_server.h"

/**
 * Initialize web server
 * Starts HTTP server on port 80
 * Registers all API endpoints
 * 
 * @return true on success
 */
bool web_server_init(void);

/**
 * Check if web server is running
 * 
 * @return true if running
 */
bool web_server_is_running(void);

/**
 * Stop and deinitialize web server
 */
void web_server_deinit(void);

/**
 * Get HTTP server handle (for advanced usage)
 * 
 * @return HTTP server handle or NULL
 */
httpd_handle_t web_server_get_handle(void);

#endif // WEB_SERVER_H
