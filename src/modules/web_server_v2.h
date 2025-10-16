#ifndef WEB_SERVER_V2_H
#define WEB_SERVER_V2_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_http_server.h"
#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize web server v2 with unified configuration system
 * @return true on success
 */
bool web_server_v2_init(void);

/**
 * Check if web server v2 is running
 * @return true if running
 */
bool web_server_v2_is_running(void);

/**
 * Deinitialize web server v2
 */
void web_server_v2_deinit(void);

/**
 * Get web server v2 handle
 * @return HTTP server handle
 */
httpd_handle_t web_server_v2_get_handle(void);

/**
 * Check authentication for request
 * @param req HTTP request
 * @return true if authenticated
 */
bool web_server_v2_check_auth(httpd_req_t *req);

/**
 * Send authentication required response
 * @param req HTTP request
 * @return ESP error code
 */
esp_err_t web_server_v2_send_auth_required(httpd_req_t *req);

/**
 * Send JSON response with proper headers
 * @param req HTTP request
 * @param json JSON object to send
 * @param status_code HTTP status code
 * @return ESP error code
 */
esp_err_t web_server_v2_send_json_response(httpd_req_t *req, cJSON *json, int status_code);

/**
 * Add CORS headers to response
 * @param req HTTP request
 */
void web_server_v2_add_cors_headers(httpd_req_t *req);

#ifdef __cplusplus
}
#endif

#endif // WEB_SERVER_V2_H