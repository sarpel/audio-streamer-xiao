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
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "esp_wifi.h"
#include "cJSON.h"
#include "mbedtls/base64.h"
#include <string.h>
#include <errno.h>

static const char *TAG = "WEB_SERVER";
static httpd_handle_t server = NULL;

// Helper function to add CORS headers to responses
void web_server_add_cors_headers(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type, Authorization");
    httpd_resp_set_hdr(req, "Access-Control-Max-Age", "86400");
}

// OPTIONS handler for CORS preflight requests
static esp_err_t options_handler(httpd_req_t *req)
{
    web_server_add_cors_headers(req);
    httpd_resp_set_status(req, "204 No Content");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

// Endpoint configuration structure for simplified registration
typedef struct
{
    const char *uri;
    httpd_method_t method;
    esp_err_t (*handler)(httpd_req_t *);
} endpoint_config_t;

// Helper function to register multiple endpoints
static void register_endpoints(httpd_handle_t server, const endpoint_config_t *endpoints, size_t count)
{
    for (size_t i = 0; i < count; i++)
    {
        httpd_uri_t uri = {
            .uri = endpoints[i].uri,
            .method = endpoints[i].method,
            .handler = endpoints[i].handler,
            .user_ctx = NULL,
            .is_websocket = false,
            .handle_ws_control_frames = false,
            .supported_subprotocol = NULL};

        esp_err_t ret = httpd_register_uri_handler(server, &uri);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to register %s %s: %s",
                     (endpoints[i].method == HTTP_GET) ? "GET" : "POST",
                     endpoints[i].uri,
                     esp_err_to_name(ret));
        }
    }
}

// Authentication functions
esp_err_t web_server_send_auth_required(httpd_req_t *req)
{
    // Add CORS headers first (critical for browser auth prompts to work)
    web_server_add_cors_headers(req);

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

// Internal wrapper for backward compatibility
static esp_err_t send_auth_required(httpd_req_t *req)
{
    return web_server_send_auth_required(req);
}

bool web_server_check_auth(httpd_req_t *req)
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

// Internal wrapper for backward compatibility
static bool check_basic_auth(httpd_req_t *req)
{
    return web_server_check_auth(req);
} // Helper to send 400 Bad Request (if not defined)
#ifndef httpd_resp_send_400
static inline esp_err_t httpd_resp_send_400(httpd_req_t *r)
{
    return httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "Bad Request");
}
#endif

// Helper function to safely receive request body with bounds checking
static esp_err_t safe_httpd_req_recv(httpd_req_t *req, char *buf, size_t buf_size, size_t *received_len)
{
    if (!req || !buf || buf_size == 0)
    {
        ESP_LOGE(TAG, "Invalid parameters for safe_httpd_req_recv");
        return ESP_FAIL;
    }

    // Check content length
    size_t content_len = req->content_len;
    if (content_len >= buf_size)
    {
        ESP_LOGE(TAG, "Request too large: %zu bytes (buffer size: %zu)", content_len, buf_size);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Request payload too large");
        return ESP_FAIL;
    }

    // Receive data
    int ret = httpd_req_recv(req, buf, buf_size - 1);
    if (ret <= 0)
    {
        ESP_LOGE(TAG, "Failed to receive request: ret=%d, errno=%d", ret, errno);
        if (ret == HTTPD_SOCK_ERR_TIMEOUT)
        {
            httpd_resp_send_408(req);
        }
        else
        {
            httpd_resp_send_500(req);
        }
        return ESP_FAIL;
    }

    // Validate received length
    if (ret >= (int)buf_size)
    {
        ESP_LOGE(TAG, "Buffer overflow prevented: received %d bytes, buffer size %zu", ret, buf_size);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    buf[ret] = '\0';
    if (received_len)
    {
        *received_len = ret;
    }

    return ESP_OK;
}

// Helper function to send JSON response
esp_err_t web_server_send_json_response(httpd_req_t *req, cJSON *json, int status_code)
{
    char *response = cJSON_Print(json);
    if (!response)
    {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    web_server_add_cors_headers(req);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, status_code == 200 ? HTTPD_200 : status_code == 400 ? HTTPD_400
                                                            : status_code == 500   ? HTTPD_500
                                                                                   : HTTPD_200);
    httpd_resp_sendstr(req, response);

    free(response);
    return ESP_OK;
}

// Internal wrapper for backward compatibility
static esp_err_t send_json_response(httpd_req_t *req, cJSON *json, int status_code)
{
    return web_server_send_json_response(req, json, status_code);
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
    if (safe_httpd_req_recv(req, buf, sizeof(buf), NULL) != ESP_OK)
    {
        return ESP_FAIL;
    }

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

    esp_err_t ret = send_json_response(req, response, 200);
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
    if (safe_httpd_req_recv(req, buf, sizeof(buf), NULL) != ESP_OK)
    {
        return ESP_FAIL;
    }

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

    esp_err_t ret = send_json_response(req, response, 200);
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
        // Get the ACTUAL connected SSID from WiFi stack, not from NVS config
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK)
        {
            cJSON_AddStringToObject(wifi, "ssid", (char *)ap_info.ssid);
            cJSON_AddNumberToObject(wifi, "rssi", ap_info.rssi);
        }
        else
        {
            // Fallback to config if WiFi stack query fails
            wifi_config_data_t wifi_cfg;
            config_manager_get_wifi(&wifi_cfg);
            cJSON_AddStringToObject(wifi, "ssid", wifi_cfg.ssid);
        }
    }
    else
    {
        cJSON_AddStringToObject(wifi, "ssid", "N/A");
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

// GET /api/config/audio - Get I2S audio configuration
static esp_err_t api_get_audio_handler(httpd_req_t *req)
{
    if (!check_basic_auth(req))
    {
        return send_auth_required(req);
    }

    i2s_config_data_t i2s;
    if (!config_manager_get_i2s(&i2s))
    {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "sample_rate", i2s.sample_rate);
    cJSON_AddNumberToObject(root, "bits_per_sample", i2s.bits_per_sample);
    cJSON_AddNumberToObject(root, "channels", i2s.channels);
    cJSON_AddNumberToObject(root, "bck_pin", i2s.bck_pin);
    cJSON_AddNumberToObject(root, "ws_pin", i2s.ws_pin);
    cJSON_AddNumberToObject(root, "data_in_pin", i2s.data_in_pin);

    esp_err_t ret = send_json_response(req, root, 200);
    cJSON_Delete(root);
    return ret;
}

// POST /api/config/audio - Update I2S audio configuration
static esp_err_t api_post_audio_handler(httpd_req_t *req)
{
    if (!check_basic_auth(req))
    {
        return send_auth_required(req);
    }

    char buf[512];
    if (safe_httpd_req_recv(req, buf, sizeof(buf), NULL) != ESP_OK)
    {
        return ESP_FAIL;
    }

    cJSON *root = cJSON_Parse(buf);
    if (!root)
    {
        httpd_resp_send_400(req);
        return ESP_FAIL;
    }

    i2s_config_data_t i2s;
    config_manager_get_i2s(&i2s);

    cJSON *sample_rate = cJSON_GetObjectItem(root, "sample_rate");
    if (sample_rate && cJSON_IsNumber(sample_rate))
    {
        i2s.sample_rate = sample_rate->valueint;
    }

    cJSON *bits_per_sample = cJSON_GetObjectItem(root, "bits_per_sample");
    if (bits_per_sample && cJSON_IsNumber(bits_per_sample))
    {
        i2s.bits_per_sample = bits_per_sample->valueint;
    }

    cJSON *channels = cJSON_GetObjectItem(root, "channels");
    if (channels && cJSON_IsNumber(channels))
    {
        i2s.channels = channels->valueint;
    }

    cJSON *bck_pin = cJSON_GetObjectItem(root, "bck_pin");
    if (bck_pin && cJSON_IsNumber(bck_pin))
    {
        i2s.bck_pin = bck_pin->valueint;
    }

    cJSON *ws_pin = cJSON_GetObjectItem(root, "ws_pin");
    if (ws_pin && cJSON_IsNumber(ws_pin))
    {
        i2s.ws_pin = ws_pin->valueint;
    }

    cJSON *data_in_pin = cJSON_GetObjectItem(root, "data_in_pin");
    if (data_in_pin && cJSON_IsNumber(data_in_pin))
    {
        i2s.data_in_pin = data_in_pin->valueint;
    }

    config_manager_set_i2s(&i2s);
    config_manager_save();
    cJSON_Delete(root);

    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "status", "success");
    cJSON_AddStringToObject(response, "message", "Audio configuration saved. Restart required.");
    cJSON_AddBoolToObject(response, "restart_required", true);

    esp_err_t ret = send_json_response(req, response, 200);
    cJSON_Delete(response);
    return ret;
}

// GET /api/config/buffer - Get buffer configuration
static esp_err_t api_get_buffer_handler(httpd_req_t *req)
{
    if (!check_basic_auth(req))
    {
        return send_auth_required(req);
    }

    buffer_config_data_t buffer;
    if (!config_manager_get_buffer(&buffer))
    {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "ring_buffer_size", buffer.ring_buffer_size);
    cJSON_AddNumberToObject(root, "dma_buf_count", buffer.dma_buf_count);
    cJSON_AddNumberToObject(root, "dma_buf_len", buffer.dma_buf_len);

    esp_err_t ret = send_json_response(req, root, 200);
    cJSON_Delete(root);
    return ret;
}

// POST /api/config/buffer - Update buffer configuration
static esp_err_t api_post_buffer_handler(httpd_req_t *req)
{
    if (!check_basic_auth(req))
    {
        return send_auth_required(req);
    }

    char buf[512];
    if (safe_httpd_req_recv(req, buf, sizeof(buf), NULL) != ESP_OK)
    {
        return ESP_FAIL;
    }

    cJSON *root = cJSON_Parse(buf);
    if (!root)
    {
        httpd_resp_send_400(req);
        return ESP_FAIL;
    }

    buffer_config_data_t buffer;
    config_manager_get_buffer(&buffer);

    cJSON *ring_buffer_size = cJSON_GetObjectItem(root, "ring_buffer_size");
    if (ring_buffer_size && cJSON_IsNumber(ring_buffer_size))
    {
        buffer.ring_buffer_size = ring_buffer_size->valueint;
    }

    cJSON *dma_buf_count = cJSON_GetObjectItem(root, "dma_buf_count");
    if (dma_buf_count && cJSON_IsNumber(dma_buf_count))
    {
        buffer.dma_buf_count = dma_buf_count->valueint;
    }

    cJSON *dma_buf_len = cJSON_GetObjectItem(root, "dma_buf_len");
    if (dma_buf_len && cJSON_IsNumber(dma_buf_len))
    {
        buffer.dma_buf_len = dma_buf_len->valueint;
    }

    config_manager_set_buffer(&buffer);
    config_manager_save();
    cJSON_Delete(root);

    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "status", "success");
    cJSON_AddStringToObject(response, "message", "Buffer configuration saved. Restart required.");
    cJSON_AddBoolToObject(response, "restart_required", true);

    esp_err_t ret = send_json_response(req, response, 200);
    cJSON_Delete(response);
    return ret;
}

// GET /api/config/tasks - Get task configuration
static esp_err_t api_get_tasks_handler(httpd_req_t *req)
{
    if (!check_basic_auth(req))
    {
        return send_auth_required(req);
    }

    task_config_data_t tasks;
    if (!config_manager_get_tasks(&tasks))
    {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "i2s_reader_priority", tasks.i2s_reader_priority);
    cJSON_AddNumberToObject(root, "tcp_sender_priority", tasks.tcp_sender_priority);
    cJSON_AddNumberToObject(root, "watchdog_priority", tasks.watchdog_priority);
    cJSON_AddNumberToObject(root, "web_server_priority", tasks.web_server_priority);
    cJSON_AddNumberToObject(root, "i2s_reader_core", tasks.i2s_reader_core);
    cJSON_AddNumberToObject(root, "tcp_sender_core", tasks.tcp_sender_core);
    cJSON_AddNumberToObject(root, "watchdog_core", tasks.watchdog_core);
    cJSON_AddNumberToObject(root, "web_server_core", tasks.web_server_core);

    esp_err_t ret = send_json_response(req, root, 200);
    cJSON_Delete(root);
    return ret;
}

// POST /api/config/tasks - Update task configuration
static esp_err_t api_post_tasks_handler(httpd_req_t *req)
{
    if (!check_basic_auth(req))
    {
        return send_auth_required(req);
    }

    char buf[512];
    if (safe_httpd_req_recv(req, buf, sizeof(buf), NULL) != ESP_OK)
    {
        return ESP_FAIL;
    }

    cJSON *root = cJSON_Parse(buf);
    if (!root)
    {
        httpd_resp_send_400(req);
        return ESP_FAIL;
    }

    task_config_data_t tasks;
    config_manager_get_tasks(&tasks);

    cJSON *i2s_reader_priority = cJSON_GetObjectItem(root, "i2s_reader_priority");
    if (i2s_reader_priority && cJSON_IsNumber(i2s_reader_priority))
    {
        tasks.i2s_reader_priority = i2s_reader_priority->valueint;
    }

    cJSON *tcp_sender_priority = cJSON_GetObjectItem(root, "tcp_sender_priority");
    if (tcp_sender_priority && cJSON_IsNumber(tcp_sender_priority))
    {
        tasks.tcp_sender_priority = tcp_sender_priority->valueint;
    }

    cJSON *watchdog_priority = cJSON_GetObjectItem(root, "watchdog_priority");
    if (watchdog_priority && cJSON_IsNumber(watchdog_priority))
    {
        tasks.watchdog_priority = watchdog_priority->valueint;
    }

    cJSON *web_server_priority = cJSON_GetObjectItem(root, "web_server_priority");
    if (web_server_priority && cJSON_IsNumber(web_server_priority))
    {
        tasks.web_server_priority = web_server_priority->valueint;
    }

    cJSON *i2s_reader_core = cJSON_GetObjectItem(root, "i2s_reader_core");
    if (i2s_reader_core && cJSON_IsNumber(i2s_reader_core))
    {
        tasks.i2s_reader_core = i2s_reader_core->valueint;
    }

    cJSON *tcp_sender_core = cJSON_GetObjectItem(root, "tcp_sender_core");
    if (tcp_sender_core && cJSON_IsNumber(tcp_sender_core))
    {
        tasks.tcp_sender_core = tcp_sender_core->valueint;
    }

    cJSON *watchdog_core = cJSON_GetObjectItem(root, "watchdog_core");
    if (watchdog_core && cJSON_IsNumber(watchdog_core))
    {
        tasks.watchdog_core = watchdog_core->valueint;
    }

    cJSON *web_server_core = cJSON_GetObjectItem(root, "web_server_core");
    if (web_server_core && cJSON_IsNumber(web_server_core))
    {
        tasks.web_server_core = web_server_core->valueint;
    }

    config_manager_set_tasks(&tasks);
    config_manager_save();
    cJSON_Delete(root);

    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "status", "success");
    cJSON_AddStringToObject(response, "message", "Task configuration saved. Restart required.");
    cJSON_AddBoolToObject(response, "restart_required", true);

    esp_err_t ret = send_json_response(req, response, 200);
    cJSON_Delete(response);
    return ret;
}

// GET /api/config/error - Get error handling configuration
static esp_err_t api_get_error_handler(httpd_req_t *req)
{
    if (!check_basic_auth(req))
    {
        return send_auth_required(req);
    }

    error_config_data_t error;
    if (!config_manager_get_error(&error))
    {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "max_reconnect_attempts", error.max_reconnect_attempts);
    cJSON_AddNumberToObject(root, "reconnect_backoff_ms", error.reconnect_backoff_ms);
    cJSON_AddNumberToObject(root, "max_reconnect_backoff_ms", error.max_reconnect_backoff_ms);
    cJSON_AddNumberToObject(root, "max_i2s_failures", error.max_i2s_failures);
    cJSON_AddNumberToObject(root, "max_buffer_overflows", error.max_buffer_overflows);
    cJSON_AddNumberToObject(root, "watchdog_timeout_sec", error.watchdog_timeout_sec);
    cJSON_AddNumberToObject(root, "ntp_resync_interval_sec", error.ntp_resync_interval_sec);

    esp_err_t ret = send_json_response(req, root, 200);
    cJSON_Delete(root);
    return ret;
}

// POST /api/config/error - Update error handling configuration
static esp_err_t api_post_error_handler(httpd_req_t *req)
{
    if (!check_basic_auth(req))
    {
        return send_auth_required(req);
    }

    char buf[512];
    if (safe_httpd_req_recv(req, buf, sizeof(buf), NULL) != ESP_OK)
    {
        return ESP_FAIL;
    }

    cJSON *root = cJSON_Parse(buf);
    if (!root)
    {
        httpd_resp_send_400(req);
        return ESP_FAIL;
    }

    error_config_data_t error;
    config_manager_get_error(&error);

    cJSON *max_reconnect_attempts = cJSON_GetObjectItem(root, "max_reconnect_attempts");
    if (max_reconnect_attempts && cJSON_IsNumber(max_reconnect_attempts))
    {
        error.max_reconnect_attempts = max_reconnect_attempts->valueint;
    }

    cJSON *reconnect_backoff_ms = cJSON_GetObjectItem(root, "reconnect_backoff_ms");
    if (reconnect_backoff_ms && cJSON_IsNumber(reconnect_backoff_ms))
    {
        error.reconnect_backoff_ms = reconnect_backoff_ms->valueint;
    }

    cJSON *max_reconnect_backoff_ms = cJSON_GetObjectItem(root, "max_reconnect_backoff_ms");
    if (max_reconnect_backoff_ms && cJSON_IsNumber(max_reconnect_backoff_ms))
    {
        error.max_reconnect_backoff_ms = max_reconnect_backoff_ms->valueint;
    }

    cJSON *max_i2s_failures = cJSON_GetObjectItem(root, "max_i2s_failures");
    if (max_i2s_failures && cJSON_IsNumber(max_i2s_failures))
    {
        error.max_i2s_failures = max_i2s_failures->valueint;
    }

    cJSON *max_buffer_overflows = cJSON_GetObjectItem(root, "max_buffer_overflows");
    if (max_buffer_overflows && cJSON_IsNumber(max_buffer_overflows))
    {
        error.max_buffer_overflows = max_buffer_overflows->valueint;
    }

    cJSON *watchdog_timeout_sec = cJSON_GetObjectItem(root, "watchdog_timeout_sec");
    if (watchdog_timeout_sec && cJSON_IsNumber(watchdog_timeout_sec))
    {
        error.watchdog_timeout_sec = watchdog_timeout_sec->valueint;
    }

    cJSON *ntp_resync_interval_sec = cJSON_GetObjectItem(root, "ntp_resync_interval_sec");
    if (ntp_resync_interval_sec && cJSON_IsNumber(ntp_resync_interval_sec))
    {
        error.ntp_resync_interval_sec = ntp_resync_interval_sec->valueint;
    }

    config_manager_set_error(&error);
    config_manager_save();
    cJSON_Delete(root);

    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "status", "success");
    cJSON_AddStringToObject(response, "message", "Error configuration saved. Restart required.");
    cJSON_AddBoolToObject(response, "restart_required", true);

    esp_err_t ret = send_json_response(req, response, 200);
    cJSON_Delete(response);
    return ret;
}

// GET /api/config/debug - Get debug configuration
static esp_err_t api_get_debug_handler(httpd_req_t *req)
{
    if (!check_basic_auth(req))
    {
        return send_auth_required(req);
    }

    debug_config_data_t debug;
    if (!config_manager_get_debug(&debug))
    {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "debug_enabled", debug.debug_enabled);
    cJSON_AddBoolToObject(root, "stack_monitoring", debug.stack_monitoring);
    cJSON_AddBoolToObject(root, "auto_reboot", debug.auto_reboot);
    cJSON_AddBoolToObject(root, "i2s_reinit", debug.i2s_reinit);
    cJSON_AddBoolToObject(root, "buffer_drain", debug.buffer_drain);

    esp_err_t ret = send_json_response(req, root, 200);
    cJSON_Delete(root);
    return ret;
}

// POST /api/config/debug - Update debug configuration
static esp_err_t api_post_debug_handler(httpd_req_t *req)
{
    if (!check_basic_auth(req))
    {
        return send_auth_required(req);
    }

    char buf[512];
    if (safe_httpd_req_recv(req, buf, sizeof(buf), NULL) != ESP_OK)
    {
        return ESP_FAIL;
    }

    cJSON *root = cJSON_Parse(buf);
    if (!root)
    {
        httpd_resp_send_400(req);
        return ESP_FAIL;
    }

    debug_config_data_t debug;
    config_manager_get_debug(&debug);

    cJSON *debug_enabled = cJSON_GetObjectItem(root, "debug_enabled");
    if (debug_enabled && cJSON_IsBool(debug_enabled))
    {
        debug.debug_enabled = cJSON_IsTrue(debug_enabled);
    }

    cJSON *stack_monitoring = cJSON_GetObjectItem(root, "stack_monitoring");
    if (stack_monitoring && cJSON_IsBool(stack_monitoring))
    {
        debug.stack_monitoring = cJSON_IsTrue(stack_monitoring);
    }

    cJSON *auto_reboot = cJSON_GetObjectItem(root, "auto_reboot");
    if (auto_reboot && cJSON_IsBool(auto_reboot))
    {
        debug.auto_reboot = cJSON_IsTrue(auto_reboot);
    }

    cJSON *i2s_reinit = cJSON_GetObjectItem(root, "i2s_reinit");
    if (i2s_reinit && cJSON_IsBool(i2s_reinit))
    {
        debug.i2s_reinit = cJSON_IsTrue(i2s_reinit);
    }

    cJSON *buffer_drain = cJSON_GetObjectItem(root, "buffer_drain");
    if (buffer_drain && cJSON_IsBool(buffer_drain))
    {
        debug.buffer_drain = cJSON_IsTrue(buffer_drain);
    }

    config_manager_set_debug(&debug);
    config_manager_save();
    cJSON_Delete(root);

    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "status", "success");
    cJSON_AddStringToObject(response, "message", "Debug configuration saved.");
    cJSON_AddBoolToObject(response, "restart_required", false);

    esp_err_t ret = send_json_response(req, response, 200);
    cJSON_Delete(response);
    return ret;
}

// GET /api/config/all - Get all configuration in single call
static esp_err_t api_get_all_config_handler(httpd_req_t *req)
{
    if (!check_basic_auth(req))
    {
        return send_auth_required(req);
    }

    cJSON *root = cJSON_CreateObject();

    // WiFi configuration
    wifi_config_data_t wifi;
    if (config_manager_get_wifi(&wifi))
    {
        cJSON *wifi_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(wifi_obj, "ssid", wifi.ssid);
        cJSON_AddStringToObject(wifi_obj, "password", "********");
        cJSON_AddBoolToObject(wifi_obj, "use_static_ip", wifi.use_static_ip);
        cJSON_AddStringToObject(wifi_obj, "static_ip", wifi.static_ip);
        cJSON_AddStringToObject(wifi_obj, "gateway", wifi.gateway);
        cJSON_AddStringToObject(wifi_obj, "subnet", wifi.subnet);
        cJSON_AddStringToObject(wifi_obj, "dns_primary", wifi.dns_primary);
        cJSON_AddStringToObject(wifi_obj, "dns_secondary", wifi.dns_secondary);
        cJSON_AddItemToObject(root, "wifi", wifi_obj);
    }

    // TCP configuration
    tcp_config_data_t tcp;
    if (config_manager_get_tcp(&tcp))
    {
        cJSON *tcp_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(tcp_obj, "server_ip", tcp.server_ip);
        cJSON_AddNumberToObject(tcp_obj, "server_port", tcp.server_port);
        cJSON_AddItemToObject(root, "tcp", tcp_obj);
    }

    // Audio configuration
    i2s_config_data_t i2s;
    if (config_manager_get_i2s(&i2s))
    {
        cJSON *audio_obj = cJSON_CreateObject();
        cJSON_AddNumberToObject(audio_obj, "sample_rate", i2s.sample_rate);
        cJSON_AddNumberToObject(audio_obj, "bits_per_sample", i2s.bits_per_sample);
        cJSON_AddNumberToObject(audio_obj, "channels", i2s.channels);
        cJSON_AddNumberToObject(audio_obj, "bck_pin", i2s.bck_pin);
        cJSON_AddNumberToObject(audio_obj, "ws_pin", i2s.ws_pin);
        cJSON_AddNumberToObject(audio_obj, "data_in_pin", i2s.data_in_pin);
        cJSON_AddItemToObject(root, "audio", audio_obj);
    }

    // Buffer configuration
    buffer_config_data_t buffer;
    if (config_manager_get_buffer(&buffer))
    {
        cJSON *buffer_obj = cJSON_CreateObject();
        cJSON_AddNumberToObject(buffer_obj, "ring_buffer_size", buffer.ring_buffer_size);
        cJSON_AddNumberToObject(buffer_obj, "dma_buf_count", buffer.dma_buf_count);
        cJSON_AddNumberToObject(buffer_obj, "dma_buf_len", buffer.dma_buf_len);
        cJSON_AddItemToObject(root, "buffer", buffer_obj);
    }

    // Task configuration
    task_config_data_t tasks;
    if (config_manager_get_tasks(&tasks))
    {
        cJSON *tasks_obj = cJSON_CreateObject();
        cJSON_AddNumberToObject(tasks_obj, "i2s_reader_priority", tasks.i2s_reader_priority);
        cJSON_AddNumberToObject(tasks_obj, "tcp_sender_priority", tasks.tcp_sender_priority);
        cJSON_AddNumberToObject(tasks_obj, "watchdog_priority", tasks.watchdog_priority);
        cJSON_AddNumberToObject(tasks_obj, "web_server_priority", tasks.web_server_priority);
        cJSON_AddNumberToObject(tasks_obj, "i2s_reader_core", tasks.i2s_reader_core);
        cJSON_AddNumberToObject(tasks_obj, "tcp_sender_core", tasks.tcp_sender_core);
        cJSON_AddNumberToObject(tasks_obj, "watchdog_core", tasks.watchdog_core);
        cJSON_AddNumberToObject(tasks_obj, "web_server_core", tasks.web_server_core);
        cJSON_AddItemToObject(root, "tasks", tasks_obj);
    }

    // Error configuration
    error_config_data_t error;
    if (config_manager_get_error(&error))
    {
        cJSON *error_obj = cJSON_CreateObject();
        cJSON_AddNumberToObject(error_obj, "max_reconnect_attempts", error.max_reconnect_attempts);
        cJSON_AddNumberToObject(error_obj, "reconnect_backoff_ms", error.reconnect_backoff_ms);
        cJSON_AddNumberToObject(error_obj, "max_reconnect_backoff_ms", error.max_reconnect_backoff_ms);
        cJSON_AddNumberToObject(error_obj, "max_i2s_failures", error.max_i2s_failures);
        cJSON_AddNumberToObject(error_obj, "max_buffer_overflows", error.max_buffer_overflows);
        cJSON_AddNumberToObject(error_obj, "watchdog_timeout_sec", error.watchdog_timeout_sec);
        cJSON_AddNumberToObject(error_obj, "ntp_resync_interval_sec", error.ntp_resync_interval_sec);
        cJSON_AddItemToObject(root, "error", error_obj);
    }

    // Debug configuration
    debug_config_data_t debug;
    if (config_manager_get_debug(&debug))
    {
        cJSON *debug_obj = cJSON_CreateObject();
        cJSON_AddBoolToObject(debug_obj, "debug_enabled", debug.debug_enabled);
        cJSON_AddBoolToObject(debug_obj, "stack_monitoring", debug.stack_monitoring);
        cJSON_AddBoolToObject(debug_obj, "auto_reboot", debug.auto_reboot);
        cJSON_AddBoolToObject(debug_obj, "i2s_reinit", debug.i2s_reinit);
        cJSON_AddBoolToObject(debug_obj, "buffer_drain", debug.buffer_drain);
        cJSON_AddItemToObject(root, "debug", debug_obj);
    }

    esp_err_t ret = send_json_response(req, root, 200);
    cJSON_Delete(root);
    return ret;
}

// POST /api/system/save - Manually save configuration to NVS
static esp_err_t api_post_save_config_handler(httpd_req_t *req)
{
    if (!check_basic_auth(req))
    {
        return send_auth_required(req);
    }

    cJSON *response = cJSON_CreateObject();

    if (config_manager_save())
    {
        cJSON_AddStringToObject(response, "status", "success");
        cJSON_AddStringToObject(response, "message", "Configuration saved to NVS successfully.");
        esp_err_t ret = send_json_response(req, response, 200);
        cJSON_Delete(response);
        return ret;
    }
    else
    {
        cJSON_AddStringToObject(response, "status", "error");
        cJSON_AddStringToObject(response, "message", "Failed to save configuration to NVS.");
        esp_err_t ret = send_json_response(req, response, 500);
        cJSON_Delete(response);
        return ret;
    }
}

// POST /api/system/load - Reload configuration from NVS
static esp_err_t api_post_load_config_handler(httpd_req_t *req)
{
    if (!check_basic_auth(req))
    {
        return send_auth_required(req);
    }

    cJSON *response = cJSON_CreateObject();

    if (config_manager_load())
    {
        cJSON_AddStringToObject(response, "status", "success");
        cJSON_AddStringToObject(response, "message", "Configuration reloaded from NVS. Restart recommended to apply changes.");
        cJSON_AddBoolToObject(response, "restart_required", true);
        esp_err_t ret = send_json_response(req, response, 200);
        cJSON_Delete(response);
        return ret;
    }
    else
    {
        cJSON_AddStringToObject(response, "status", "error");
        cJSON_AddStringToObject(response, "message", "Failed to reload configuration from NVS.");
        esp_err_t ret = send_json_response(req, response, 500);
        cJSON_Delete(response);
        return ret;
    }
}

// External symbols for embedded web files
// HTML files
extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[] asm("_binary_index_html_end");
extern const uint8_t config_html_start[] asm("_binary_config_html_start");
extern const uint8_t config_html_end[] asm("_binary_config_html_end");
extern const uint8_t monitor_html_start[] asm("_binary_monitor_html_start");
extern const uint8_t monitor_html_end[] asm("_binary_monitor_html_end");
extern const uint8_t ota_html_start[] asm("_binary_ota_html_start");
extern const uint8_t ota_html_end[] asm("_binary_ota_html_end");
extern const uint8_t logs_html_start[] asm("_binary_logs_html_start");
extern const uint8_t logs_html_end[] asm("_binary_logs_html_end");
extern const uint8_t network_html_start[] asm("_binary_network_html_start");
extern const uint8_t network_html_end[] asm("_binary_network_html_end");

// CSS files
extern const uint8_t style_css_start[] asm("_binary_style_css_start");
extern const uint8_t style_css_end[] asm("_binary_style_css_end");

// JS files
extern const uint8_t api_js_start[] asm("_binary_api_js_start");
extern const uint8_t api_js_end[] asm("_binary_api_js_end");
extern const uint8_t utils_js_start[] asm("_binary_utils_js_start");
extern const uint8_t utils_js_end[] asm("_binary_utils_js_end");
extern const uint8_t app_js_start[] asm("_binary_app_js_start");
extern const uint8_t app_js_end[] asm("_binary_app_js_end");
extern const uint8_t config_js_start[] asm("_binary_config_js_start");
extern const uint8_t config_js_end[] asm("_binary_config_js_end");
extern const uint8_t monitor_js_start[] asm("_binary_monitor_js_start");
extern const uint8_t monitor_js_end[] asm("_binary_monitor_js_end");
extern const uint8_t ota_js_start[] asm("_binary_ota_js_start");
extern const uint8_t ota_js_end[] asm("_binary_ota_js_end");
extern const uint8_t logs_js_start[] asm("_binary_logs_js_start");
extern const uint8_t logs_js_end[] asm("_binary_logs_js_end");
extern const uint8_t network_js_start[] asm("_binary_network_js_start");
extern const uint8_t network_js_end[] asm("_binary_network_js_end");

// Helper function to determine MIME type from file extension
static const char *get_mime_type(const char *path)
{
    const char *ext = strrchr(path, '.');
    if (!ext)
    {
        return "application/octet-stream";
    }

    if (strcmp(ext, ".html") == 0)
    {
        return "text/html";
    }
    else if (strcmp(ext, ".css") == 0)
    {
        return "text/css";
    }
    else if (strcmp(ext, ".js") == 0)
    {
        return "application/javascript";
    }
    else if (strcmp(ext, ".json") == 0)
    {
        return "application/json";
    }
    else if (strcmp(ext, ".png") == 0)
    {
        return "image/png";
    }
    else if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0)
    {
        return "image/jpeg";
    }
    else if (strcmp(ext, ".ico") == 0)
    {
        return "image/x-icon";
    }
    else if (strcmp(ext, ".svg") == 0)
    {
        return "image/svg+xml";
    }

    return "application/octet-stream";
}

// Generic file serving handler
static esp_err_t serve_embedded_file(httpd_req_t *req, const uint8_t *start, const uint8_t *end, const char *path)
{
    size_t file_size = end - start;
    const char *mime_type = get_mime_type(path);

    // Files embedded with TEXT mode have a null terminator that should not be sent
    // Check if the last byte is null and exclude it if so
    if (file_size > 0 && start[file_size - 1] == '\0')
    {
        file_size--;
    }

    ESP_LOGD(TAG, "Serving %s (%zu bytes, %s)", path, file_size, mime_type);

    web_server_add_cors_headers(req);
    httpd_resp_set_type(req, mime_type);
    httpd_resp_send(req, (const char *)start, file_size);

    return ESP_OK;
}

// Root handler - serves index.html
static esp_err_t root_handler(httpd_req_t *req)
{
    return serve_embedded_file(req, index_html_start, index_html_end, "index.html");
}

// HTML file handlers
static esp_err_t config_html_handler(httpd_req_t *req)
{
    return serve_embedded_file(req, config_html_start, config_html_end, "config.html");
}

static esp_err_t monitor_html_handler(httpd_req_t *req)
{
    return serve_embedded_file(req, monitor_html_start, monitor_html_end, "monitor.html");
}

static esp_err_t ota_html_handler(httpd_req_t *req)
{
    // Require authentication for OTA page
    if (!check_basic_auth(req))
    {
        return send_auth_required(req);
    }

    return serve_embedded_file(req, ota_html_start, ota_html_end, "ota.html");
}

static esp_err_t logs_html_handler(httpd_req_t *req)
{
    return serve_embedded_file(req, logs_html_start, logs_html_end, "logs.html");
}

static esp_err_t network_html_handler(httpd_req_t *req)
{
    return serve_embedded_file(req, network_html_start, network_html_end, "network.html");
}

// CSS file handlers
static esp_err_t css_style_handler(httpd_req_t *req)
{
    return serve_embedded_file(req, style_css_start, style_css_end, "style.css");
}

// JS file handlers
static esp_err_t js_api_handler(httpd_req_t *req)
{
    return serve_embedded_file(req, api_js_start, api_js_end, "api.js");
}

static esp_err_t js_utils_handler(httpd_req_t *req)
{
    return serve_embedded_file(req, utils_js_start, utils_js_end, "utils.js");
}

static esp_err_t js_app_handler(httpd_req_t *req)
{
    return serve_embedded_file(req, app_js_start, app_js_end, "app.js");
}

static esp_err_t js_config_handler(httpd_req_t *req)
{
    return serve_embedded_file(req, config_js_start, config_js_end, "config.js");
}

static esp_err_t js_monitor_handler(httpd_req_t *req)
{
    return serve_embedded_file(req, monitor_js_start, monitor_js_end, "monitor.js");
}

static esp_err_t js_ota_handler(httpd_req_t *req)
{
    return serve_embedded_file(req, ota_js_start, ota_js_end, "ota.js");
}

static esp_err_t js_logs_handler(httpd_req_t *req)
{
    return serve_embedded_file(req, logs_js_start, logs_js_end, "logs.js");
}

static esp_err_t js_network_handler(httpd_req_t *req)
{
    return serve_embedded_file(req, network_js_start, network_js_end, "network.js");
}

// 404 handler
static esp_err_t notfound_handler(httpd_req_t *req, httpd_err_code_t err)
{
    httpd_resp_set_status(req, "404 Not Found");
    httpd_resp_set_type(req, "text/html");
    const char *html = "<!DOCTYPE html><html><head><title>404 Not Found</title></head>"
                       "<body><h1>404 Not Found</h1>"
                       "<p>The requested resource was not found on this server.</p>"
                       "<p><a href=\"/\">Return to Home</a></p>"
                       "</body></html>";
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
    config.max_uri_handlers = 48; // Increased to accommodate all web UI endpoints + OTA + OPTIONS + 404
    config.max_open_sockets = 4;
    config.lru_purge_enable = true;
    config.stack_size = 8192;

    ESP_LOGI(TAG, "Starting web server on port %d", config.server_port);

    if (httpd_start(&server, &config) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to start web server");
        return false;
    }

    // Define all endpoints in a compact array
    const endpoint_config_t endpoints[] = {
        // Root
        {"/", HTTP_GET, root_handler},

        // HTML pages
        {"/config.html", HTTP_GET, config_html_handler},
        {"/monitor.html", HTTP_GET, monitor_html_handler},
        {"/ota.html", HTTP_GET, ota_html_handler},
        {"/logs.html", HTTP_GET, logs_html_handler},
        {"/network.html", HTTP_GET, network_html_handler},

        // CSS files
        {"/css/style.css", HTTP_GET, css_style_handler},

        // JS files
        {"/js/api.js", HTTP_GET, js_api_handler},
        {"/js/utils.js", HTTP_GET, js_utils_handler},
        {"/js/app.js", HTTP_GET, js_app_handler},
        {"/js/config.js", HTTP_GET, js_config_handler},
        {"/js/monitor.js", HTTP_GET, js_monitor_handler},
        {"/js/ota.js", HTTP_GET, js_ota_handler},
        {"/js/logs.js", HTTP_GET, js_logs_handler},
        {"/js/network.js", HTTP_GET, js_network_handler},

        // Configuration endpoints
        {"/api/config/wifi", HTTP_GET, api_get_wifi_handler},
        {"/api/config/wifi", HTTP_POST, api_post_wifi_handler},
        {"/api/config/tcp", HTTP_GET, api_get_tcp_handler},
        {"/api/config/tcp", HTTP_POST, api_post_tcp_handler},
        {"/api/config/audio", HTTP_GET, api_get_audio_handler},
        {"/api/config/audio", HTTP_POST, api_post_audio_handler},
        {"/api/config/buffer", HTTP_GET, api_get_buffer_handler},
        {"/api/config/buffer", HTTP_POST, api_post_buffer_handler},
        {"/api/config/tasks", HTTP_GET, api_get_tasks_handler},
        {"/api/config/tasks", HTTP_POST, api_post_tasks_handler},
        {"/api/config/error", HTTP_GET, api_get_error_handler},
        {"/api/config/error", HTTP_POST, api_post_error_handler},
        {"/api/config/debug", HTTP_GET, api_get_debug_handler},
        {"/api/config/debug", HTTP_POST, api_post_debug_handler},
        {"/api/config/all", HTTP_GET, api_get_all_config_handler},

        // System endpoints
        {"/api/system/status", HTTP_GET, api_get_status_handler},
        {"/api/system/info", HTTP_GET, api_get_info_handler},
        {"/api/system/restart", HTTP_POST, api_post_restart_handler},
        {"/api/system/factory-reset", HTTP_POST, api_post_factory_reset_handler},
        {"/api/system/save", HTTP_POST, api_post_save_config_handler},
        {"/api/system/load", HTTP_POST, api_post_load_config_handler}};

    // Register all endpoints with the refactored helper
    register_endpoints(server, endpoints, sizeof(endpoints) / sizeof(endpoints[0]));

    // Register OTA endpoints (these handle their own auth and CORS)
    ota_handler_register_endpoints(server);

    // Register OPTIONS handler for CORS preflight on all API endpoints
    httpd_uri_t options_uri = {
        .uri = "/api/*",
        .method = HTTP_OPTIONS,
        .handler = options_handler,
        .user_ctx = NULL,
        .is_websocket = false,
        .handle_ws_control_frames = false,
        .supported_subprotocol = NULL};
    httpd_register_uri_handler(server, &options_uri);

    // Register 404 handler (must be last)
    httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, notfound_handler);

    ESP_LOGI(TAG, "Web server started successfully with %zu endpoints", sizeof(endpoints) / sizeof(endpoints[0]));
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
