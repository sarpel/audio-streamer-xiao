#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <stdbool.h>
#include "esp_http_server.h"

// Forward declarations
struct cJSON;
typedef struct cJSON cJSON;

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

/**
 * Add CORS headers to HTTP response
 * Should be called for all API endpoints
 *
 * @param req HTTP request handle
 */
void web_server_add_cors_headers(httpd_req_t *req);

/**
 * Check HTTP Basic Authentication
 * Validates username and password from Authorization header
 *
 * @param req HTTP request handle
 * @return true if authenticated, false otherwise
 */
bool web_server_check_auth(httpd_req_t *req);

/**
 * Send authentication required response (401)
 * Includes WWW-Authenticate header
 *
 * @param req HTTP request handle
 * @return ESP_OK
 */
esp_err_t web_server_send_auth_required(httpd_req_t *req);

/**
 * Send JSON response with CORS headers
 * Automatically adds CORS headers and Content-Type
 *
 * @param req HTTP request handle
 * @param json cJSON object to send
 * @param status HTTP status code
 * @return ESP_OK on success
 */
esp_err_t web_server_send_json_response(httpd_req_t *req, cJSON *json, int status);

#endif // WEB_SERVER_H
