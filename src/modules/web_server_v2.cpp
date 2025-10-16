#include "../config.h"
#include "web_server_v2.h"
#include "config_manager_v2.h"
#include "network_manager.h"
#include "tcp_streamer.h"
#include "udp_streamer.h"
#include "buffer_manager.h"
#include "i2s_handler.h"
#include "ota_handler.h"
#include "performance_monitor.h"
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

static const char *TAG = "WEB_SERVER_V2";
static httpd_handle_t server = NULL;

// Helper function to add CORS headers to responses
void web_server_v2_add_cors_headers(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type, Authorization");
    httpd_resp_set_hdr(req, "Access-Control-Max-Age", "86400");
}

// OPTIONS handler for CORS preflight requests
static esp_err_t options_handler(httpd_req_t *req)
{
    web_server_v2_add_cors_headers(req);
    httpd_resp_set_status(req, "204 No Content");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

// Authentication functions
esp_err_t web_server_v2_send_auth_required(httpd_req_t *req)
{
    // Add CORS headers first (critical for browser auth prompts to work)
    web_server_v2_add_cors_headers(req);

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

bool web_server_v2_check_auth(httpd_req_t *req)
{
    // Get stored credentials from unified configuration
    char username[32];
    char password[64];

    if (!config_manager_v2_get_field(CONFIG_FIELD_AUTH_USERNAME, username, sizeof(username)) ||
        !config_manager_v2_get_field(CONFIG_FIELD_AUTH_PASSWORD, password, sizeof(password)))
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
    const char *req_username = (const char *)decoded;
    const char *req_password = colon + 1;

    // Compare credentials
    bool valid = (strcmp(req_username, username) == 0 && strcmp(req_password, password) == 0);

    if (!valid)
    {
        ESP_LOGW(TAG, "Invalid credentials");
    }

    return valid;
}

// Helper function to send JSON response
esp_err_t web_server_v2_send_json_response(httpd_req_t *req, cJSON *json, int status_code)
{
    char *response = cJSON_Print(json);
    if (!response)
    {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    web_server_v2_add_cors_headers(req);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, status_code == 200 ? HTTPD_200 : status_code == 400 ? HTTPD_400
                                                            : status_code == 500   ? HTTPD_500
                                                                                   : HTTPD_200);
    httpd_resp_sendstr(req, response);

    free(response);
    return ESP_OK;
}

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

// GET /api/config/network - Get network configuration (WiFi + TCP/UDP)
static esp_err_t api_get_network_handler(httpd_req_t *req)
{
    if (!web_server_v2_check_auth(req))
    {
        return web_server_v2_send_auth_required(req);
    }

    cJSON *root = cJSON_CreateObject();

    // WiFi configuration
    cJSON *wifi = cJSON_CreateObject();
    char ssid[32], static_ip[16], gateway[16], subnet[16];
    char dns_primary[16], dns_secondary[16];
    char use_static_ip_str[8];

    if (config_manager_v2_get_field(CONFIG_FIELD_WIFI_SSID, ssid, sizeof(ssid))) {
        cJSON_AddStringToObject(wifi, "ssid", ssid);
    }
    cJSON_AddStringToObject(wifi, "password", "********"); // Hide password

    if (config_manager_v2_get_field(CONFIG_FIELD_WIFI_USE_STATIC_IP, use_static_ip_str, sizeof(use_static_ip_str)) &&
        config_manager_v2_get_field(CONFIG_FIELD_WIFI_STATIC_IP, static_ip, sizeof(static_ip)) &&
        config_manager_v2_get_field(CONFIG_FIELD_WIFI_GATEWAY, gateway, sizeof(gateway)) &&
        config_manager_v2_get_field(CONFIG_FIELD_WIFI_SUBNET, subnet, sizeof(subnet))) {
        cJSON_AddBoolToObject(wifi, "use_static_ip", (strcmp(use_static_ip_str, "1") == 0));
        cJSON_AddStringToObject(wifi, "static_ip", static_ip);
        cJSON_AddStringToObject(wifi, "gateway", gateway);
        cJSON_AddStringToObject(wifi, "subnet", subnet);

        if (config_manager_v2_get_field(CONFIG_FIELD_WIFI_DNS_PRIMARY, dns_primary, sizeof(dns_primary))) {
            cJSON_AddStringToObject(wifi, "dns_primary", dns_primary);
        }
        if (config_manager_v2_get_field(CONFIG_FIELD_WIFI_DNS_SECONDARY, dns_secondary, sizeof(dns_secondary))) {
            cJSON_AddStringToObject(wifi, "dns_secondary", dns_secondary);
        }
    }
    cJSON_AddItemToObject(root, "wifi", wifi);

    // TCP/UDP server configuration
    cJSON *server = cJSON_CreateObject();
    char tcp_ip[16], udp_ip[16];
    char tcp_port_str[8], udp_port_str[8];

    if (config_manager_v2_get_field(CONFIG_FIELD_TCP_SERVER_IP, tcp_ip, sizeof(tcp_ip))) {
        cJSON_AddStringToObject(server, "tcp_ip", tcp_ip);
    }
    if (config_manager_v2_get_field(CONFIG_FIELD_UDP_SERVER_IP, udp_ip, sizeof(udp_ip))) {
        cJSON_AddStringToObject(server, "udp_ip", udp_ip);
    }
    if (config_manager_v2_get_field(CONFIG_FIELD_TCP_SERVER_PORT, tcp_port_str, sizeof(tcp_port_str))) {
        cJSON_AddNumberToObject(server, "tcp_port", atoi(tcp_port_str));
    }
    if (config_manager_v2_get_field(CONFIG_FIELD_UDP_SERVER_PORT, udp_port_str, sizeof(udp_port_str))) {
        cJSON_AddNumberToObject(server, "udp_port", atoi(udp_port_str));
    }

    char protocol_str[8];
    if (config_manager_v2_get_field(CONFIG_FIELD_STREAMING_PROTOCOL, protocol_str, sizeof(protocol_str))) {
        int protocol = atoi(protocol_str);
        const char *protocol_name = (protocol == 0) ? "TCP" : (protocol == 1) ? "UDP" : "BOTH";
        cJSON_AddStringToObject(server, "protocol", protocol_name);
    }

    cJSON_AddItemToObject(root, "server", server);

    esp_err_t ret = web_server_v2_send_json_response(req, root, 200);
    cJSON_Delete(root);
    return ret;
}

// POST /api/config/network - Update network configuration
static esp_err_t api_post_network_handler(httpd_req_t *req)
{
    if (!web_server_v2_check_auth(req))
    {
        return web_server_v2_send_auth_required(req);
    }

    char buf[1024];
    if (safe_httpd_req_recv(req, buf, sizeof(buf), NULL) != ESP_OK)
    {
        return ESP_FAIL;
    }

    cJSON *root = cJSON_Parse(buf);
    if (!root)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "status", "success");
    cJSON_AddStringToObject(response, "message", "Network configuration updated. Restart required to apply changes.");
    cJSON_AddBoolToObject(response, "restart_required", true);

    cJSON_Delete(root);

    esp_err_t ret = web_server_v2_send_json_response(req, response, 200);
    cJSON_Delete(response);
    return ret;
}

// GET /api/config/audio - Get audio configuration
static esp_err_t api_get_audio_handler(httpd_req_t *req)
{
    if (!web_server_v2_check_auth(req))
    {
        return web_server_v2_send_auth_required(req);
    }

    cJSON *root = cJSON_CreateObject();

    // Audio configuration (fixed format, only GPIO pins configurable)
    cJSON *audio = cJSON_CreateObject();
    char bck_pin_str[8], ws_pin_str[8], data_in_pin_str[8];

    if (config_manager_v2_get_field(CONFIG_FIELD_AUDIO_BCK_PIN, bck_pin_str, sizeof(bck_pin_str))) {
        cJSON_AddNumberToObject(audio, "bck_pin", atoi(bck_pin_str));
    }
    if (config_manager_v2_get_field(CONFIG_FIELD_AUDIO_WS_PIN, ws_pin_str, sizeof(ws_pin_str))) {
        cJSON_AddNumberToObject(audio, "ws_pin", atoi(ws_pin_str));
    }
    if (config_manager_v2_get_field(CONFIG_FIELD_AUDIO_DATA_IN_PIN, data_in_pin_str, sizeof(data_in_pin_str))) {
        cJSON_AddNumberToObject(audio, "data_in_pin", atoi(data_in_pin_str));
    }

    // Fixed values (read-only)
    cJSON_AddNumberToObject(audio, "sample_rate", SAMPLE_RATE);
    cJSON_AddNumberToObject(audio, "bits_per_sample", BITS_PER_SAMPLE);
    cJSON_AddNumberToObject(audio, "channels", CHANNELS);
    cJSON_AddStringToObject(audio, "format", "PCM 16-bit mono");

    // Calculate data rate
    uint32_t data_rate_bps = SAMPLE_RATE * CHANNELS * (BITS_PER_SAMPLE / 8);
    cJSON_AddNumberToObject(audio, "data_rate_bps", data_rate_bps);
    cJSON_AddNumberToObject(audio, "data_rate_kbps", data_rate_bps / 1024.0);

    cJSON_AddItemToObject(root, "audio", audio);

    esp_err_t ret = web_server_v2_send_json_response(req, root, 200);
    cJSON_Delete(root);
    return ret;
}

// POST /api/config/audio - Update audio configuration (GPIO pins only)
static esp_err_t api_post_audio_handler(httpd_req_t *req)
{
    if (!web_server_v2_check_auth(req))
    {
        return web_server_v2_send_auth_required(req);
    }

    char buf[512];
    if (safe_httpd_req_recv(req, buf, sizeof(buf), NULL) != ESP_OK)
    {
        return ESP_FAIL;
    }

    cJSON *root = cJSON_Parse(buf);
    if (!root)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    bool changed = false;
    config_validation_result_t result;

    // Update GPIO pins if provided
    cJSON *bck_pin = cJSON_GetObjectItem(root, "bck_pin");
    if (bck_pin && cJSON_IsNumber(bck_pin)) {
        char pin_str[8];
        snprintf(pin_str, sizeof(pin_str), "%d", bck_pin->valueint);
        if (config_manager_v2_set_field(CONFIG_FIELD_AUDIO_BCK_PIN, pin_str, &result)) {
            changed = true;
        }
    }

    cJSON *ws_pin = cJSON_GetObjectItem(root, "ws_pin");
    if (ws_pin && cJSON_IsNumber(ws_pin)) {
        char pin_str[8];
        snprintf(pin_str, sizeof(pin_str), "%d", ws_pin->valueint);
        if (config_manager_v2_set_field(CONFIG_FIELD_AUDIO_WS_PIN, pin_str, &result)) {
            changed = true;
        }
    }

    cJSON *data_in_pin = cJSON_GetObjectItem(root, "data_in_pin");
    if (data_in_pin && cJSON_IsNumber(data_in_pin)) {
        char pin_str[8];
        snprintf(pin_str, sizeof(pin_str), "%d", data_in_pin->valueint);
        if (config_manager_v2_set_field(CONFIG_FIELD_AUDIO_DATA_IN_PIN, pin_str, &result)) {
            changed = true;
        }
    }

    // Save configuration if changed
    if (changed) {
        config_manager_v2_save();
    }

    cJSON_Delete(root);

    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "status", "success");
    if (changed) {
        cJSON_AddStringToObject(response, "message", "Audio GPIO configuration saved. Restart required to apply changes.");
        cJSON_AddBoolToObject(response, "restart_required", true);
    } else {
        cJSON_AddStringToObject(response, "message", "No changes detected.");
        cJSON_AddBoolToObject(response, "restart_required", false);
    }

    esp_err_t ret = web_server_v2_send_json_response(req, response, 200);
    cJSON_Delete(response);
    return ret;
}

// GET /api/config/all - Get all configuration
static esp_err_t api_get_all_config_handler(httpd_req_t *req)
{
    if (!web_server_v2_check_auth(req))
    {
        return web_server_v2_send_auth_required(req);
    }

    // Export configuration as JSON from unified config
    char json_buffer[8192];
    if (!config_manager_v2_export_json(json_buffer, sizeof(json_buffer)))
    {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    cJSON *root = cJSON_Parse(json_buffer);
    if (!root)
    {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    esp_err_t ret = web_server_v2_send_json_response(req, root, 200);
    cJSON_Delete(root);
    return ret;
}

// POST /api/config/all - Update multiple configuration fields
static esp_err_t api_post_all_config_handler(httpd_req_t *req)
{
    if (!web_server_v2_check_auth(req))
    {
        return web_server_v2_send_auth_required(req);
    }

    char buf[4096];
    if (safe_httpd_req_recv(req, buf, sizeof(buf), NULL) != ESP_OK)
    {
        return ESP_FAIL;
    }

    cJSON *root = cJSON_Parse(buf);
    if (!root)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    // Import configuration into unified config
    config_validation_result_t results[10];
    size_t issue_count = config_manager_v2_import_json(buf, true, results, 10);

    cJSON *response = cJSON_CreateObject();
    if (issue_count == 0) {
        // Save configuration
        if (config_manager_v2_save()) {
            cJSON_AddStringToObject(response, "status", "success");
            cJSON_AddStringToObject(response, "message", "Configuration updated successfully. Restart required to apply changes.");
            cJSON_AddBoolToObject(response, "restart_required", true);
        } else {
            cJSON_AddStringToObject(response, "status", "error");
            cJSON_AddStringToObject(response, "message", "Failed to save configuration");
            web_server_v2_send_json_response(req, response, 500);
            cJSON_Delete(response);
            cJSON_Delete(root);
            return ESP_FAIL;
        }
    } else {
        cJSON_AddStringToObject(response, "status", "error");
        cJSON_AddStringToObject(response, "message", "Configuration validation failed");
        cJSON_AddNumberToObject(response, "validation_errors", issue_count);

        // Add validation error details
        cJSON *errors = cJSON_CreateArray();
        for (size_t i = 0; i < issue_count; i++) {
            cJSON *error = cJSON_CreateObject();
            cJSON_AddNumberToObject(error, "field_id", results[i].field_id);
            cJSON_AddStringToObject(error, "error_message", results[i].error_message);
            cJSON_AddItemToArray(errors, error);
        }
        cJSON_AddItemToObject(response, "errors", errors);
    }

    cJSON_Delete(root);

    esp_err_t ret = web_server_v2_send_json_response(req, response, (issue_count == 0) ? 200 : 400);
    cJSON_Delete(response);
    return ret;
}

// GET /api/system/status - Get system status
static esp_err_t api_get_status_handler(httpd_req_t *req)
{
    if (!web_server_v2_check_auth(req))
    {
        return web_server_v2_send_auth_required(req);
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
            char ssid[32];
            if (config_manager_v2_get_field(CONFIG_FIELD_WIFI_SSID, ssid, sizeof(ssid))) {
                cJSON_AddStringToObject(wifi, "ssid", ssid);
            }
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
    char tcp_ip[16], tcp_port_str[8];
    if (config_manager_v2_get_field(CONFIG_FIELD_TCP_SERVER_IP, tcp_ip, sizeof(tcp_ip)) &&
        config_manager_v2_get_field(CONFIG_FIELD_TCP_SERVER_PORT, tcp_port_str, sizeof(tcp_port_str))) {
        char server_str[32];
        snprintf(server_str, sizeof(server_str), "%s:%s", tcp_ip, tcp_port_str);
        cJSON_AddStringToObject(tcp, "server", server_str);
    }

    uint64_t bytes_sent;
    uint32_t reconnects;
    tcp_streamer_get_stats(&bytes_sent, &reconnects);
    cJSON_AddNumberToObject(tcp, "bytes_sent", bytes_sent);
    cJSON_AddNumberToObject(tcp, "reconnects", reconnects);
    cJSON_AddItemToObject(root, "tcp", tcp);

    // Audio status
    cJSON *audio = cJSON_CreateObject();
    cJSON_AddNumberToObject(audio, "sample_rate", SAMPLE_RATE);
    cJSON_AddNumberToObject(audio, "bits_per_sample", BITS_PER_SAMPLE);
    cJSON_AddNumberToObject(audio, "channels", CHANNELS);
    cJSON_AddItemToObject(root, "audio", audio);

    // Buffer status
    cJSON *buffer = cJSON_CreateObject();
    cJSON_AddNumberToObject(buffer, "usage_percent", buffer_manager_usage_percent());
    char buffer_size_str[16];
    if (config_manager_v2_get_field(CONFIG_FIELD_BUFFER_RING_SIZE, buffer_size_str, sizeof(buffer_size_str))) {
        cJSON_AddNumberToObject(buffer, "size_kb", atoi(buffer_size_str) / 1024);
    }
    cJSON_AddItemToObject(root, "buffer", buffer);

    // Memory status
    cJSON *memory = cJSON_CreateObject();
    cJSON_AddNumberToObject(memory, "free_heap", esp_get_free_heap_size());
    cJSON_AddNumberToObject(memory, "min_free_heap", esp_get_minimum_free_heap_size());
    cJSON_AddItemToObject(root, "memory", memory);

    // Configuration version
    cJSON_AddNumberToObject(root, "config_version", config_manager_v2_get_version());
    cJSON_AddBoolToObject(root, "has_unsaved_changes", config_manager_v2_has_unsaved_changes());

    esp_err_t ret = web_server_v2_send_json_response(req, root, 200);
    cJSON_Delete(root);
    return ret;
}

// GET /api/system/info - Get device info
static esp_err_t api_get_info_handler(httpd_req_t *req)
{
    if (!web_server_v2_check_auth(req))
    {
        return web_server_v2_send_auth_required(req);
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

    cJSON_AddStringToObject(root, "firmware_version", "2.0.0");
    cJSON_AddStringToObject(root, "config_system", "unified_v2");

    esp_err_t ret = web_server_v2_send_json_response(req, root, 200);
    cJSON_Delete(root);
    return ret;
}

// POST /api/system/restart - Restart device
static esp_err_t api_post_restart_handler(httpd_req_t *req)
{
    if (!web_server_v2_check_auth(req))
    {
        return web_server_v2_send_auth_required(req);
    }

    // Save any unsaved changes before restart
    if (config_manager_v2_has_unsaved_changes()) {
        config_manager_v2_save();
    }

    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "status", "success");
    cJSON_AddStringToObject(response, "message", "Device will restart in 2 seconds");

    web_server_v2_send_json_response(req, response, 200);
    cJSON_Delete(response);

    // Schedule restart
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();

    return ESP_OK;
}

// POST /api/system/factory-reset - Factory reset
static esp_err_t api_post_factory_reset_handler(httpd_req_t *req)
{
    if (!web_server_v2_check_auth(req))
    {
        return web_server_v2_send_auth_required(req);
    }

    ESP_LOGI(TAG, "Factory reset requested");

    if (!config_manager_v2_reset_to_factory())
    {
        cJSON *response = cJSON_CreateObject();
        cJSON_AddStringToObject(response, "status", "error");
        cJSON_AddStringToObject(response, "message", "Failed to reset configuration");

        web_server_v2_send_json_response(req, response, 500);
        cJSON_Delete(response);
        return ESP_FAIL;
    }

    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "status", "success");
    cJSON_AddStringToObject(response, "message", "Factory reset complete. Device will restart.");

    web_server_v2_send_json_response(req, response, 200);
    cJSON_Delete(response);

    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();

    return ESP_OK;
}

// POST /api/system/save - Save configuration
static esp_err_t api_post_save_config_handler(httpd_req_t *req)
{
    if (!web_server_v2_check_auth(req))
    {
        return web_server_v2_send_auth_required(req);
    }

    cJSON *response = cJSON_CreateObject();

    if (config_manager_v2_save())
    {
        cJSON_AddStringToObject(response, "status", "success");
        cJSON_AddStringToObject(response, "message", "Configuration saved to NVS successfully.");
        esp_err_t ret = web_server_v2_send_json_response(req, response, 200);
        cJSON_Delete(response);
        return ret;
    }
    else
    {
        cJSON_AddStringToObject(response, "status", "error");
        cJSON_AddStringToObject(response, "message", "Failed to save configuration to NVS.");
        esp_err_t ret = web_server_v2_send_json_response(req, response, 500);
        cJSON_Delete(response);
        return ret;
    }
}

// POST /api/system/load - Load configuration
static esp_err_t api_post_load_config_handler(httpd_req_t *req)
{
    if (!web_server_v2_check_auth(req))
    {
        return web_server_v2_send_auth_required(req);
    }

    cJSON *response = cJSON_CreateObject();

    if (config_manager_v2_load())
    {
        cJSON_AddStringToObject(response, "status", "success");
        cJSON_AddStringToObject(response, "message", "Configuration reloaded from NVS. Restart recommended to apply changes.");
        cJSON_AddBoolToObject(response, "restart_required", true);
        esp_err_t ret = web_server_v2_send_json_response(req, response, 200);
        cJSON_Delete(response);
        return ret;
    }
    else
    {
        cJSON_AddStringToObject(response, "status", "error");
        cJSON_AddStringToObject(response, "message", "Failed to reload configuration from NVS.");
        esp_err_t ret = web_server_v2_send_json_response(req, response, 500);
        cJSON_Delete(response);
        return ret;
    }
}

// GET /api/system/validate - Validate current configuration
static esp_err_t api_get_validate_handler(httpd_req_t *req)
{
    if (!web_server_v2_check_auth(req))
    {
        return web_server_v2_send_auth_required(req);
    }

    cJSON *response = cJSON_CreateObject();
    config_validation_result_t results[10];
    size_t issue_count = config_manager_v2_validate(results, 10);

    if (issue_count == 0)
    {
        cJSON_AddStringToObject(response, "status", "valid");
        cJSON_AddStringToObject(response, "message", "Current configuration is valid");
        cJSON_AddNumberToObject(response, "version", config_manager_v2_get_version());

        // Add configuration summary
        char ssid[32];
        if (config_manager_v2_get_field(CONFIG_FIELD_WIFI_SSID, ssid, sizeof(ssid))) {
            cJSON *wifi_info = cJSON_CreateObject();
            cJSON_AddStringToObject(wifi_info, "ssid", ssid);
            char use_static_str[8];
            if (config_manager_v2_get_field(CONFIG_FIELD_WIFI_USE_STATIC_IP, use_static_str, sizeof(use_static_str))) {
                cJSON_AddBoolToObject(wifi_info, "static_ip", (strcmp(use_static_str, "1") == 0));
            }
            cJSON_AddItemToObject(response, "wifi_summary", wifi_info);
        }

        char tcp_ip[16], tcp_port_str[8];
        if (config_manager_v2_get_field(CONFIG_FIELD_TCP_SERVER_IP, tcp_ip, sizeof(tcp_ip)) &&
            config_manager_v2_get_field(CONFIG_FIELD_TCP_SERVER_PORT, tcp_port_str, sizeof(tcp_port_str))) {
            char server_str[32];
            snprintf(server_str, sizeof(server_str), "%s:%s", tcp_ip, tcp_port_str);
            cJSON_AddStringToObject(response, "tcp_server", server_str);
        }

        esp_err_t ret = web_server_v2_send_json_response(req, response, 200);
        cJSON_Delete(response);
        return ret;
    }
    else
    {
        cJSON_AddStringToObject(response, "status", "invalid");
        cJSON_AddStringToObject(response, "message", "Current configuration has validation errors");
        cJSON_AddNumberToObject(response, "error_count", issue_count);

        cJSON *errors = cJSON_CreateArray();
        for (size_t i = 0; i < issue_count; i++) {
            cJSON *error = cJSON_CreateObject();
            cJSON_AddNumberToObject(error, "field_id", results[i].field_id);
            cJSON_AddStringToObject(error, "error_message", results[i].error_message);
            cJSON_AddItemToArray(errors, error);
        }
        cJSON_AddItemToObject(response, "errors", errors);

        esp_err_t ret = web_server_v2_send_json_response(req, response, 400);
        cJSON_Delete(response);
        return ret;
    }
}

// Initialize web server v2
bool web_server_v2_init(void)
{
    if (server != NULL)
    {
        ESP_LOGW(TAG, "Web server v2 already running");
        return true;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.max_uri_handlers = 24;
    config.max_open_sockets = 4;
    config.lru_purge_enable = true;
    config.stack_size = 8192;

    ESP_LOGI(TAG, "Starting web server v2 on port %d", config.server_port);

    if (httpd_start(&server, &config) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to start web server v2");
        return false;
    }

    // Register OPTIONS handler for CORS
    httpd_uri_t options_uri = {
        .uri = "/api/*",
        .method = HTTP_OPTIONS,
        .handler = options_handler,
        .user_ctx = NULL,
        .is_websocket = false,
        .handle_ws_control_frames = false,
        .supported_subprotocol = NULL
    };
    httpd_register_uri_handler(server, &options_uri);

    // Register API endpoints
    const httpd_uri_t endpoints[] = {
        // Configuration endpoints
        {.uri = "/api/config/network", .method = HTTP_GET, .handler = api_get_network_handler, .user_ctx = NULL, .is_websocket = false, .handle_ws_control_frames = false, .supported_subprotocol = NULL},
        {.uri = "/api/config/network", .method = HTTP_POST, .handler = api_post_network_handler, .user_ctx = NULL, .is_websocket = false, .handle_ws_control_frames = false, .supported_subprotocol = NULL},
        {.uri = "/api/config/audio", .method = HTTP_GET, .handler = api_get_audio_handler, .user_ctx = NULL, .is_websocket = false, .handle_ws_control_frames = false, .supported_subprotocol = NULL},
        {.uri = "/api/config/audio", .method = HTTP_POST, .handler = api_post_audio_handler, .user_ctx = NULL, .is_websocket = false, .handle_ws_control_frames = false, .supported_subprotocol = NULL},
        {.uri = "/api/config/all", .method = HTTP_GET, .handler = api_get_all_config_handler, .user_ctx = NULL, .is_websocket = false, .handle_ws_control_frames = false, .supported_subprotocol = NULL},
        {.uri = "/api/config/all", .method = HTTP_POST, .handler = api_post_all_config_handler, .user_ctx = NULL, .is_websocket = false, .handle_ws_control_frames = false, .supported_subprotocol = NULL},

        // System endpoints
        {.uri = "/api/system/status", .method = HTTP_GET, .handler = api_get_status_handler, .user_ctx = NULL, .is_websocket = false, .handle_ws_control_frames = false, .supported_subprotocol = NULL},
        {.uri = "/api/system/info", .method = HTTP_GET, .handler = api_get_info_handler, .user_ctx = NULL, .is_websocket = false, .handle_ws_control_frames = false, .supported_subprotocol = NULL},
        {.uri = "/api/system/restart", .method = HTTP_POST, .handler = api_post_restart_handler, .user_ctx = NULL, .is_websocket = false, .handle_ws_control_frames = false, .supported_subprotocol = NULL},
        {.uri = "/api/system/factory-reset", .method = HTTP_POST, .handler = api_post_factory_reset_handler, .user_ctx = NULL, .is_websocket = false, .handle_ws_control_frames = false, .supported_subprotocol = NULL},
        {.uri = "/api/system/save", .method = HTTP_POST, .handler = api_post_save_config_handler, .user_ctx = NULL, .is_websocket = false, .handle_ws_control_frames = false, .supported_subprotocol = NULL},
        {.uri = "/api/system/load", .method = HTTP_POST, .handler = api_post_load_config_handler, .user_ctx = NULL, .is_websocket = false, .handle_ws_control_frames = false, .supported_subprotocol = NULL},
        {.uri = "/api/system/validate", .method = HTTP_GET, .handler = api_get_validate_handler, .user_ctx = NULL, .is_websocket = false, .handle_ws_control_frames = false, .supported_subprotocol = NULL},
    };

    // Register all endpoints
    for (size_t i = 0; i < sizeof(endpoints) / sizeof(endpoints[0]); i++) {
        esp_err_t ret = httpd_register_uri_handler(server, &endpoints[i]);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register endpoint %s", endpoints[i].uri);
        }
    }

    // Register OTA endpoints (from existing OTA handler)
    ota_handler_register_endpoints(server);

    ESP_LOGI(TAG, "Web server v2 started successfully with %zu endpoints", sizeof(endpoints) / sizeof(endpoints[0]));
    return true;
}

bool web_server_v2_is_running(void)
{
    return (server != NULL);
}

void web_server_v2_deinit(void)
{
    if (server != NULL)
    {
        httpd_stop(server);
        server = NULL;
        ESP_LOGI(TAG, "Web server v2 stopped");
    }
}

httpd_handle_t web_server_v2_get_handle(void)
{
    return server;
}