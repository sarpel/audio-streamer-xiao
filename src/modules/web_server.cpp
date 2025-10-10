#include "web_server.h"
#include "config_manager.h"
#include "network_manager.h"
#include "tcp_streamer.h"
#include "buffer_manager.h"
#include "i2s_handler.h"
#include "ota_handler.h"
#include "../config.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_timer.h"
#include "esp_mac.h"
#include "cJSON.h"
#include "mbedtls/base64.h"
#include <string.h>

static const char *TAG = "WEB_SERVER";
static httpd_handle_t server = NULL;

// Authentication functions
static esp_err_t send_auth_required(httpd_req_t *req)
{
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"Audio Streamer\"");

    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "status", "error");
    cJSON_AddStringToObject(response, "message", "Authentication required");

    char *json_str = cJSON_Print(response);
    httpd_resp_sendstr(req, json_str);

    free(json_str);
    cJSON_Delete(response);
    return ESP_FAIL;
}

static bool check_basic_auth(httpd_req_t *req)
{
    // Get stored credentials
    auth_config_data_t auth;
    if (!config_manager_get_auth(&auth))
    {
        ESP_LOGW(TAG, "Failed to get auth config");
        return false;
    }

    // Get Authorization header
    char auth_header[256];
    esp_err_t err = httpd_req_get_hdr_value_str(req, "Authorization", auth_header, sizeof(auth_header));
    if (err != ESP_OK)
    {
        ESP_LOGD(TAG, "No Authorization header");
        return false;
    }

    // Check for "Basic " prefix
    if (strncmp(auth_header, "Basic ", 6) != 0)
    {
        ESP_LOGW(TAG, "Invalid Authorization header format");
        return false;
    }

    // Decode base64
    unsigned char decoded[128];
    size_t decoded_len;
    int ret = mbedtls_base64_decode(decoded, sizeof(decoded), &decoded_len,
                                    (const unsigned char *)(auth_header + 6),
                                    strlen(auth_header + 6));
    if (ret != 0)
    {
        ESP_LOGW(TAG, "Failed to decode base64 auth: %d", ret);
        return false;
    }

    decoded[decoded_len] = '\0';

    // Split username:password
    char *colon = strchr((char *)decoded, ':');
    if (!colon)
    {
        ESP_LOGW(TAG, "Invalid auth format (no colon)");
        return false;
    }

    *colon = '\0';
    const char *username = (const char *)decoded;
    const char *password = colon + 1;

    // Compare credentials
    bool valid = (strcmp(username, auth.username) == 0 && strcmp(password, auth.password) == 0);

    if (!valid)
    {
        ESP_LOGW(TAG, "Invalid credentials");
    }

    return valid;
}

// Helper to send 400 Bad Request (if not defined)
#ifndef httpd_resp_send_400
static inline esp_err_t httpd_resp_send_400(httpd_req_t *r)
{
    return httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "Bad Request");
}
#endif

// Helper function to send JSON response
static esp_err_t send_json_response(httpd_req_t *req, cJSON *json, int status_code)
{
    char *response = cJSON_Print(json);
    if (!response)
    {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, status_code == 200 ? HTTPD_200 : status_code == 400 ? HTTPD_400
                                                            : status_code == 500   ? HTTPD_500
                                                                                   : HTTPD_200);
    httpd_resp_sendstr(req, response);

    free(response);
    return ESP_OK;
}

// GET /api/config/wifi - Get WiFi configuration
static esp_err_t api_get_wifi_handler(httpd_req_t *req)
{
    // Check authentication
    if (!check_basic_auth(req))
    {
        return send_auth_required(req);
    }

    wifi_config_data_t wifi;
    if (!config_manager_get_wifi(&wifi))
    {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "ssid", wifi.ssid);
    cJSON_AddStringToObject(root, "password", "********"); // Don't expose password
    cJSON_AddBoolToObject(root, "use_static_ip", wifi.use_static_ip);
    cJSON_AddStringToObject(root, "static_ip", wifi.static_ip);
    cJSON_AddStringToObject(root, "gateway", wifi.gateway);
    cJSON_AddStringToObject(root, "subnet", wifi.subnet);
    cJSON_AddStringToObject(root, "dns_primary", wifi.dns_primary);
    cJSON_AddStringToObject(root, "dns_secondary", wifi.dns_secondary);

    esp_err_t ret = send_json_response(req, root, 200);
    cJSON_Delete(root);
    return ret;
}

// POST /api/config/wifi - Update WiFi configuration
static esp_err_t api_post_wifi_handler(httpd_req_t *req)
{
    // Check authentication
    if (!check_basic_auth(req))
    {
        return send_auth_required(req);
    }

    char buf[512];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0)
    {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (!root)
    {
        httpd_resp_send_400(req);
        return ESP_FAIL;
    }

    wifi_config_data_t wifi;
    config_manager_get_wifi(&wifi); // Get current config

    // Update fields if present
    cJSON *ssid = cJSON_GetObjectItem(root, "ssid");
    if (ssid && cJSON_IsString(ssid))
    {
        strncpy(wifi.ssid, ssid->valuestring, sizeof(wifi.ssid) - 1);
    }

    cJSON *password = cJSON_GetObjectItem(root, "password");
    if (password && cJSON_IsString(password) && strcmp(password->valuestring, "********") != 0)
    {
        strncpy(wifi.password, password->valuestring, sizeof(wifi.password) - 1);
    }

    cJSON *use_static = cJSON_GetObjectItem(root, "use_static_ip");
    if (use_static && cJSON_IsBool(use_static))
    {
        wifi.use_static_ip = cJSON_IsTrue(use_static);
    }

    cJSON *static_ip = cJSON_GetObjectItem(root, "static_ip");
    if (static_ip && cJSON_IsString(static_ip))
    {
        strncpy(wifi.static_ip, static_ip->valuestring, sizeof(wifi.static_ip) - 1);
    }

    cJSON *gateway = cJSON_GetObjectItem(root, "gateway");
    if (gateway && cJSON_IsString(gateway))
    {
        strncpy(wifi.gateway, gateway->valuestring, sizeof(wifi.gateway) - 1);
    }

    cJSON *subnet = cJSON_GetObjectItem(root, "subnet");
    if (subnet && cJSON_IsString(subnet))
    {
        strncpy(wifi.subnet, subnet->valuestring, sizeof(wifi.subnet) - 1);
    }

    cJSON *dns_primary = cJSON_GetObjectItem(root, "dns_primary");
    if (dns_primary && cJSON_IsString(dns_primary))
    {
        strncpy(wifi.dns_primary, dns_primary->valuestring, sizeof(wifi.dns_primary) - 1);
    }

    cJSON *dns_secondary = cJSON_GetObjectItem(root, "dns_secondary");
    if (dns_secondary && cJSON_IsString(dns_secondary))
    {
        strncpy(wifi.dns_secondary, dns_secondary->valuestring, sizeof(wifi.dns_secondary) - 1);
    }

    config_manager_set_wifi(&wifi);
    config_manager_save();
    cJSON_Delete(root);

    // Send response
    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "status", "success");
    cJSON_AddStringToObject(response, "message", "WiFi configuration saved. Restart required.");
    cJSON_AddBoolToObject(response, "restart_required", true);

    ret = send_json_response(req, response, 200);
    cJSON_Delete(response);
    return ret;
}

// GET /api/config/tcp - Get TCP configuration
static esp_err_t api_get_tcp_handler(httpd_req_t *req)
{
    // Check authentication
    if (!check_basic_auth(req))
    {
        return send_auth_required(req);
    }

    tcp_config_data_t tcp;
    if (!config_manager_get_tcp(&tcp))
    {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "server_ip", tcp.server_ip);
    cJSON_AddNumberToObject(root, "server_port", tcp.server_port);

    esp_err_t ret = send_json_response(req, root, 200);
    cJSON_Delete(root);
    return ret;
}

// POST /api/config/tcp - Update TCP configuration
static esp_err_t api_post_tcp_handler(httpd_req_t *req)
{
    // Check authentication
    if (!check_basic_auth(req))
    {
        return send_auth_required(req);
    }

    char buf[256];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0)
    {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (!root)
    {
        httpd_resp_send_400(req);
        return ESP_FAIL;
    }

    tcp_config_data_t tcp;
    config_manager_get_tcp(&tcp);

    cJSON *server_ip = cJSON_GetObjectItem(root, "server_ip");
    if (server_ip && cJSON_IsString(server_ip))
    {
        strncpy(tcp.server_ip, server_ip->valuestring, sizeof(tcp.server_ip) - 1);
    }

    cJSON *server_port = cJSON_GetObjectItem(root, "server_port");
    if (server_port && cJSON_IsNumber(server_port))
    {
        tcp.server_port = server_port->valueint;
    }

    config_manager_set_tcp(&tcp);
    config_manager_save();
    cJSON_Delete(root);

    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "status", "success");
    cJSON_AddStringToObject(response, "message", "TCP configuration saved. Restart required.");
    cJSON_AddBoolToObject(response, "restart_required", true);

    ret = send_json_response(req, response, 200);
    cJSON_Delete(response);
    return ret;
}

// GET /api/system/status - Get system status
static esp_err_t api_get_status_handler(httpd_req_t *req)
{
    // Check authentication
    if (!check_basic_auth(req))
    {
        return send_auth_required(req);
    }

    cJSON *root = cJSON_CreateObject();

    // Uptime
    cJSON_AddNumberToObject(root, "uptime_sec", esp_timer_get_time() / 1000000);

    // WiFi status
    cJSON *wifi = cJSON_CreateObject();
    cJSON_AddBoolToObject(wifi, "connected", network_manager_is_connected());
    if (network_manager_is_connected())
    {
        wifi_config_data_t wifi_cfg;
        config_manager_get_wifi(&wifi_cfg);
        cJSON_AddStringToObject(wifi, "ssid", wifi_cfg.ssid);
        // Add more WiFi details here (RSSI, IP, etc.)
    }
    cJSON_AddItemToObject(root, "wifi", wifi);

    // TCP status
    cJSON *tcp = cJSON_CreateObject();
    cJSON_AddBoolToObject(tcp, "connected", tcp_streamer_is_connected());
    tcp_config_data_t tcp_cfg;
    config_manager_get_tcp(&tcp_cfg);
    char server_str[32];
    snprintf(server_str, sizeof(server_str), "%s:%d", tcp_cfg.server_ip, tcp_cfg.server_port);
    cJSON_AddStringToObject(tcp, "server", server_str);

    uint64_t bytes_sent;
    uint32_t reconnects;
    tcp_streamer_get_stats(&bytes_sent, &reconnects);
    cJSON_AddNumberToObject(tcp, "bytes_sent", bytes_sent);
    cJSON_AddNumberToObject(tcp, "reconnects", reconnects);
    cJSON_AddItemToObject(root, "tcp", tcp);

    // Audio status
    cJSON *audio = cJSON_CreateObject();
    i2s_config_data_t i2s_cfg;
    config_manager_get_i2s(&i2s_cfg);
    cJSON_AddNumberToObject(audio, "sample_rate", i2s_cfg.sample_rate);
    cJSON_AddItemToObject(root, "audio", audio);

    // Buffer status
    cJSON *buffer = cJSON_CreateObject();
    buffer_config_data_t buf_cfg;
    config_manager_get_buffer(&buf_cfg);
    cJSON_AddNumberToObject(buffer, "size_kb", buf_cfg.ring_buffer_size / 1024);
    cJSON_AddNumberToObject(buffer, "usage_percent", buffer_manager_usage_percent());
    cJSON_AddItemToObject(root, "buffer", buffer);

    // Memory status
    cJSON *memory = cJSON_CreateObject();
    cJSON_AddNumberToObject(memory, "free_heap", esp_get_free_heap_size());
    cJSON_AddNumberToObject(memory, "min_free_heap", esp_get_minimum_free_heap_size());
    cJSON_AddItemToObject(root, "memory", memory);

    esp_err_t ret = send_json_response(req, root, 200);
    cJSON_Delete(root);
    return ret;
}

// GET /api/system/info - Get device info
static esp_err_t api_get_info_handler(httpd_req_t *req)
{
    // Check authentication
    if (!check_basic_auth(req))
    {
        return send_auth_required(req);
    }

    cJSON *root = cJSON_CreateObject();

    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);

    cJSON_AddStringToObject(root, "chip_model", "ESP32-S3");
    cJSON_AddNumberToObject(root, "cores", chip_info.cores);
    cJSON_AddNumberToObject(root, "revision", chip_info.revision);
    cJSON_AddStringToObject(root, "idf_version", esp_get_idf_version());

    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    cJSON_AddStringToObject(root, "mac_address", mac_str);

    cJSON_AddStringToObject(root, "firmware_version", "1.0.0");

    esp_err_t ret = send_json_response(req, root, 200);
    cJSON_Delete(root);
    return ret;
}

// POST /api/system/restart - Restart device
static esp_err_t api_post_restart_handler(httpd_req_t *req)
{
    // Check authentication
    if (!check_basic_auth(req))
    {
        return send_auth_required(req);
    }

    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "status", "success");
    cJSON_AddStringToObject(response, "message", "Device will restart in 2 seconds");

    send_json_response(req, response, 200);
    cJSON_Delete(response);

    // Schedule restart
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();

    return ESP_OK;
}

// POST /api/system/factory-reset - Factory reset
static esp_err_t api_post_factory_reset_handler(httpd_req_t *req)
{
    // Check authentication
    if (!check_basic_auth(req))
    {
        return send_auth_required(req);
    }

    ESP_LOGI(TAG, "Factory reset requested");

    if (!config_manager_reset_to_factory())
    {
        cJSON *response = cJSON_CreateObject();
        cJSON_AddStringToObject(response, "status", "error");
        cJSON_AddStringToObject(response, "message", "Failed to reset configuration");

        send_json_response(req, response, 500);
        cJSON_Delete(response);
        return ESP_FAIL;
    }

    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "status", "success");
    cJSON_AddStringToObject(response, "message", "Factory reset complete. Device will restart.");

    send_json_response(req, response, 200);
    cJSON_Delete(response);

    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();

    return ESP_OK;
}

// Root handler - serves basic info
static esp_err_t root_handler(httpd_req_t *req)
{
    const char *html = "<!DOCTYPE html><html><head><title>ESP32 Audio Streamer</title></head>"
                       "<body><h1>ESP32-S3 Audio Streamer</h1>"
                       "<p>Web UI Coming Soon!</p>"
                       "<h2>API Endpoints:</h2>"
                       "<ul>"
                       "<li>GET /api/config/wifi - WiFi configuration</li>"
                       "<li>POST /api/config/wifi - Update WiFi</li>"
                       "<li>GET /api/config/tcp - TCP configuration</li>"
                       "<li>POST /api/config/tcp - Update TCP</li>"
                       "<li>GET /api/system/status - System status</li>"
                       "<li>GET /api/system/info - Device info</li>"
                       "<li>POST /api/system/restart - Restart device</li>"
                       "<li>POST /api/system/factory-reset - Factory reset</li>"
                       "</ul></body></html>";

    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr(req, html);
    return ESP_OK;
}

bool web_server_init(void)
{
    if (server != NULL)
    {
        ESP_LOGW(TAG, "Web server already running");
        return true;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.max_uri_handlers = 20; // ensure enough slots for all endpoints
    config.max_open_sockets = 4;
    config.lru_purge_enable = true;
    config.stack_size = 8192;

    ESP_LOGI(TAG, "Starting web server on port %d", config.server_port);

    if (httpd_start(&server, &config) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to start web server");
        return false;
    }

    // Register URI handlers
    httpd_uri_t root_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_handler,
        .user_ctx = NULL,
        .is_websocket = false,
        .handle_ws_control_frames = false,
        .supported_subprotocol = NULL};
    httpd_register_uri_handler(server, &root_uri);

    // Configuration endpoints
    httpd_uri_t api_get_wifi = {
        .uri = "/api/config/wifi",
        .method = HTTP_GET,
        .handler = api_get_wifi_handler,
        .user_ctx = NULL,
        .is_websocket = false,
        .handle_ws_control_frames = false,
        .supported_subprotocol = NULL};
    httpd_register_uri_handler(server, &api_get_wifi);

    httpd_uri_t api_post_wifi = {
        .uri = "/api/config/wifi",
        .method = HTTP_POST,
        .handler = api_post_wifi_handler,
        .user_ctx = NULL,
        .is_websocket = false,
        .handle_ws_control_frames = false,
        .supported_subprotocol = NULL};
    httpd_register_uri_handler(server, &api_post_wifi);

    httpd_uri_t api_get_tcp = {
        .uri = "/api/config/tcp",
        .method = HTTP_GET,
        .handler = api_get_tcp_handler,
        .user_ctx = NULL,
        .is_websocket = false,
        .handle_ws_control_frames = false,
        .supported_subprotocol = NULL};
    httpd_register_uri_handler(server, &api_get_tcp);

    httpd_uri_t api_post_tcp = {
        .uri = "/api/config/tcp",
        .method = HTTP_POST,
        .handler = api_post_tcp_handler,
        .user_ctx = NULL,
        .is_websocket = false,
        .handle_ws_control_frames = false,
        .supported_subprotocol = NULL};
    httpd_register_uri_handler(server, &api_post_tcp);

    // System endpoints
    httpd_uri_t api_get_status = {
        .uri = "/api/system/status",
        .method = HTTP_GET,
        .handler = api_get_status_handler,
        .user_ctx = NULL,
        .is_websocket = false,
        .handle_ws_control_frames = false,
        .supported_subprotocol = NULL};
    httpd_register_uri_handler(server, &api_get_status);

    httpd_uri_t api_get_info = {
        .uri = "/api/system/info",
        .method = HTTP_GET,
        .handler = api_get_info_handler,
        .user_ctx = NULL,
        .is_websocket = false,
        .handle_ws_control_frames = false,
        .supported_subprotocol = NULL};
    httpd_register_uri_handler(server, &api_get_info);

    httpd_uri_t api_post_restart = {
        .uri = "/api/system/restart",
        .method = HTTP_POST,
        .handler = api_post_restart_handler,
        .user_ctx = NULL,
        .is_websocket = false,
        .handle_ws_control_frames = false,
        .supported_subprotocol = NULL};
    httpd_register_uri_handler(server, &api_post_restart);

    httpd_uri_t api_post_factory_reset = {
        .uri = "/api/system/factory-reset",
        .method = HTTP_POST,
        .handler = api_post_factory_reset_handler,
        .user_ctx = NULL,
        .is_websocket = false,
        .handle_ws_control_frames = false,
        .supported_subprotocol = NULL};
    httpd_register_uri_handler(server, &api_post_factory_reset);

    // Register OTA endpoints
    ota_handler_register_endpoints(server);

    ESP_LOGI(TAG, "Web server started successfully");
    return true;
}

bool web_server_is_running(void)
{
    return (server != NULL);
}

void web_server_deinit(void)
{
    if (server != NULL)
    {
        httpd_stop(server);
        server = NULL;
        ESP_LOGI(TAG, "Web server stopped");
    }
}

httpd_handle_t web_server_get_handle(void)
{
    return server;
}
