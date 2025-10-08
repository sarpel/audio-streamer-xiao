#include "web_server.h"
#include "config_manager.h"
#include "network_manager.h"
#include "tcp_streamer.h"
#include "buffer_manager.h"
#include "i2s_handler.h"
#include "../config.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_timer.h"
#include "esp_mac.h"
#include "cJSON.h"
#include <string.h>

static const char* TAG = "WEB_SERVER";
static httpd_handle_t server = NULL;

// Helper to send 400 Bad Request (if not defined)
#ifndef httpd_resp_send_400
static inline esp_err_t httpd_resp_send_400(httpd_req_t *r) {
    return httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "Bad Request");
}
#endif

// Helper function to send JSON response
static esp_err_t send_json_response(httpd_req_t *req, cJSON *json, int status_code) {
    char *response = cJSON_Print(json);
    if (!response) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, status_code == 200 ? HTTPD_200 : 
                               status_code == 400 ? HTTPD_400 :
                               status_code == 500 ? HTTPD_500 : HTTPD_200);
    httpd_resp_sendstr(req, response);
    
    free(response);
    return ESP_OK;
}

// GET /api/config/wifi - Get WiFi configuration
static esp_err_t api_get_wifi_handler(httpd_req_t *req) {
    wifi_config_data_t wifi;
    if (!config_manager_get_wifi(&wifi)) {
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
static esp_err_t api_post_wifi_handler(httpd_req_t *req) {
    char buf[512];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_400(req);
        return ESP_FAIL;
    }

    wifi_config_data_t wifi;
    config_manager_get_wifi(&wifi); // Get current config

    // Update fields if present
    cJSON *ssid = cJSON_GetObjectItem(root, "ssid");
    if (ssid && cJSON_IsString(ssid)) {
        strncpy(wifi.ssid, ssid->valuestring, sizeof(wifi.ssid) - 1);
    }

    cJSON *password = cJSON_GetObjectItem(root, "password");
    if (password && cJSON_IsString(password) && strcmp(password->valuestring, "********") != 0) {
        strncpy(wifi.password, password->valuestring, sizeof(wifi.password) - 1);
    }

    cJSON *use_static = cJSON_GetObjectItem(root, "use_static_ip");
    if (use_static && cJSON_IsBool(use_static)) {
        wifi.use_static_ip = cJSON_IsTrue(use_static);
    }

    cJSON *static_ip = cJSON_GetObjectItem(root, "static_ip");
    if (static_ip && cJSON_IsString(static_ip)) {
        strncpy(wifi.static_ip, static_ip->valuestring, sizeof(wifi.static_ip) - 1);
    }

    cJSON *gateway = cJSON_GetObjectItem(root, "gateway");
    if (gateway && cJSON_IsString(gateway)) {
        strncpy(wifi.gateway, gateway->valuestring, sizeof(wifi.gateway) - 1);
    }

    cJSON *subnet = cJSON_GetObjectItem(root, "subnet");
    if (subnet && cJSON_IsString(subnet)) {
        strncpy(wifi.subnet, subnet->valuestring, sizeof(wifi.subnet) - 1);
    }

    cJSON *dns_primary = cJSON_GetObjectItem(root, "dns_primary");
    if (dns_primary && cJSON_IsString(dns_primary)) {
        strncpy(wifi.dns_primary, dns_primary->valuestring, sizeof(wifi.dns_primary) - 1);
    }

    cJSON *dns_secondary = cJSON_GetObjectItem(root, "dns_secondary");
    if (dns_secondary && cJSON_IsString(dns_secondary)) {
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
static esp_err_t api_get_tcp_handler(httpd_req_t *req) {
    tcp_config_data_t tcp;
    if (!config_manager_get_tcp(&tcp)) {
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
static esp_err_t api_post_tcp_handler(httpd_req_t *req) {
    char buf[256];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_400(req);
        return ESP_FAIL;
    }

    tcp_config_data_t tcp;
    config_manager_get_tcp(&tcp);

    cJSON *server_ip = cJSON_GetObjectItem(root, "server_ip");
    if (server_ip && cJSON_IsString(server_ip)) {
        strncpy(tcp.server_ip, server_ip->valuestring, sizeof(tcp.server_ip) - 1);
    }

    cJSON *server_port = cJSON_GetObjectItem(root, "server_port");
    if (server_port && cJSON_IsNumber(server_port)) {
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
static esp_err_t api_get_status_handler(httpd_req_t *req) {
    cJSON *root = cJSON_CreateObject();
    
    // Uptime
    cJSON_AddNumberToObject(root, "uptime_sec", esp_timer_get_time() / 1000000);

    // WiFi status
    cJSON *wifi = cJSON_CreateObject();
    cJSON_AddBoolToObject(wifi, "connected", network_manager_is_connected());
    if (network_manager_is_connected()) {
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
static esp_err_t api_get_info_handler(httpd_req_t *req) {
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
static esp_err_t api_post_restart_handler(httpd_req_t *req) {
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
static esp_err_t api_post_factory_reset_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Factory reset requested");
    
    if (!config_manager_reset_to_factory()) {
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

// GET /api/config/audio - Get audio/I2S configuration
static esp_err_t api_get_audio_handler(httpd_req_t *req) {
    i2s_config_data_t i2s;
    if (!config_manager_get_i2s(&i2s)) {
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

// POST /api/config/audio - Update audio/I2S configuration
static esp_err_t api_post_audio_handler(httpd_req_t *req) {
    char buf[256];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_400(req);
        return ESP_FAIL;
    }

    i2s_config_data_t i2s;
    config_manager_get_i2s(&i2s);

    cJSON *sample_rate = cJSON_GetObjectItem(root, "sample_rate");
    if (sample_rate && cJSON_IsNumber(sample_rate)) {
        i2s.sample_rate = sample_rate->valueint;
    }

    cJSON *bits = cJSON_GetObjectItem(root, "bits_per_sample");
    if (bits && cJSON_IsNumber(bits)) {
        i2s.bits_per_sample = bits->valueint;
    }

    cJSON *channels = cJSON_GetObjectItem(root, "channels");
    if (channels && cJSON_IsNumber(channels)) {
        i2s.channels = channels->valueint;
    }

    cJSON *bck = cJSON_GetObjectItem(root, "bck_pin");
    if (bck && cJSON_IsNumber(bck)) {
        i2s.bck_pin = bck->valueint;
    }

    cJSON *ws = cJSON_GetObjectItem(root, "ws_pin");
    if (ws && cJSON_IsNumber(ws)) {
        i2s.ws_pin = ws->valueint;
    }

    cJSON *data_in = cJSON_GetObjectItem(root, "data_in_pin");
    if (data_in && cJSON_IsNumber(data_in)) {
        i2s.data_in_pin = data_in->valueint;
    }

    config_manager_set_i2s(&i2s);
    config_manager_save();
    cJSON_Delete(root);

    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "status", "success");
    cJSON_AddStringToObject(response, "message", "Audio configuration saved. Restart required.");
    cJSON_AddBoolToObject(response, "restart_required", true);

    ret = send_json_response(req, response, 200);
    cJSON_Delete(response);
    return ret;
}

// GET /api/config/buffer - Get buffer configuration
static esp_err_t api_get_buffer_handler(httpd_req_t *req) {
    buffer_config_data_t buffer;
    if (!config_manager_get_buffer(&buffer)) {
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
static esp_err_t api_post_buffer_handler(httpd_req_t *req) {
    char buf[256];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_400(req);
        return ESP_FAIL;
    }

    buffer_config_data_t buffer;
    config_manager_get_buffer(&buffer);

    cJSON *ring_size = cJSON_GetObjectItem(root, "ring_buffer_size");
    if (ring_size && cJSON_IsNumber(ring_size)) {
        buffer.ring_buffer_size = ring_size->valueint;
    }

    cJSON *dma_count = cJSON_GetObjectItem(root, "dma_buf_count");
    if (dma_count && cJSON_IsNumber(dma_count)) {
        buffer.dma_buf_count = dma_count->valueint;
    }

    cJSON *dma_len = cJSON_GetObjectItem(root, "dma_buf_len");
    if (dma_len && cJSON_IsNumber(dma_len)) {
        buffer.dma_buf_len = dma_len->valueint;
    }

    config_manager_set_buffer(&buffer);
    config_manager_save();
    cJSON_Delete(root);

    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "status", "success");
    cJSON_AddStringToObject(response, "message", "Buffer configuration saved. Restart required.");
    cJSON_AddBoolToObject(response, "restart_required", true);

    ret = send_json_response(req, response, 200);
    cJSON_Delete(response);
    return ret;
}

// GET /api/config/tasks - Get task configuration
static esp_err_t api_get_tasks_handler(httpd_req_t *req) {
    task_config_data_t tasks;
    if (!config_manager_get_tasks(&tasks)) {
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
static esp_err_t api_post_tasks_handler(httpd_req_t *req) {
    char buf[512];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_400(req);
        return ESP_FAIL;
    }

    task_config_data_t tasks;
    config_manager_get_tasks(&tasks);

    cJSON *item;
    if ((item = cJSON_GetObjectItem(root, "i2s_reader_priority")) && cJSON_IsNumber(item))
        tasks.i2s_reader_priority = item->valueint;
    if ((item = cJSON_GetObjectItem(root, "tcp_sender_priority")) && cJSON_IsNumber(item))
        tasks.tcp_sender_priority = item->valueint;
    if ((item = cJSON_GetObjectItem(root, "watchdog_priority")) && cJSON_IsNumber(item))
        tasks.watchdog_priority = item->valueint;
    if ((item = cJSON_GetObjectItem(root, "web_server_priority")) && cJSON_IsNumber(item))
        tasks.web_server_priority = item->valueint;
    if ((item = cJSON_GetObjectItem(root, "i2s_reader_core")) && cJSON_IsNumber(item))
        tasks.i2s_reader_core = item->valueint;
    if ((item = cJSON_GetObjectItem(root, "tcp_sender_core")) && cJSON_IsNumber(item))
        tasks.tcp_sender_core = item->valueint;
    if ((item = cJSON_GetObjectItem(root, "watchdog_core")) && cJSON_IsNumber(item))
        tasks.watchdog_core = item->valueint;
    if ((item = cJSON_GetObjectItem(root, "web_server_core")) && cJSON_IsNumber(item))
        tasks.web_server_core = item->valueint;

    config_manager_set_tasks(&tasks);
    config_manager_save();
    cJSON_Delete(root);

    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "status", "success");
    cJSON_AddStringToObject(response, "message", "Task configuration saved. Restart required.");
    cJSON_AddBoolToObject(response, "restart_required", true);

    ret = send_json_response(req, response, 200);
    cJSON_Delete(response);
    return ret;
}

// GET /api/config/error - Get error handling configuration
static esp_err_t api_get_error_handler(httpd_req_t *req) {
    error_config_data_t error;
    if (!config_manager_get_error(&error)) {
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
static esp_err_t api_post_error_handler(httpd_req_t *req) {
    char buf[512];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_400(req);
        return ESP_FAIL;
    }

    error_config_data_t error;
    config_manager_get_error(&error);

    cJSON *item;
    if ((item = cJSON_GetObjectItem(root, "max_reconnect_attempts")) && cJSON_IsNumber(item))
        error.max_reconnect_attempts = item->valueint;
    if ((item = cJSON_GetObjectItem(root, "reconnect_backoff_ms")) && cJSON_IsNumber(item))
        error.reconnect_backoff_ms = item->valueint;
    if ((item = cJSON_GetObjectItem(root, "max_reconnect_backoff_ms")) && cJSON_IsNumber(item))
        error.max_reconnect_backoff_ms = item->valueint;
    if ((item = cJSON_GetObjectItem(root, "max_i2s_failures")) && cJSON_IsNumber(item))
        error.max_i2s_failures = item->valueint;
    if ((item = cJSON_GetObjectItem(root, "max_buffer_overflows")) && cJSON_IsNumber(item))
        error.max_buffer_overflows = item->valueint;
    if ((item = cJSON_GetObjectItem(root, "watchdog_timeout_sec")) && cJSON_IsNumber(item))
        error.watchdog_timeout_sec = item->valueint;
    if ((item = cJSON_GetObjectItem(root, "ntp_resync_interval_sec")) && cJSON_IsNumber(item))
        error.ntp_resync_interval_sec = item->valueint;

    config_manager_set_error(&error);
    config_manager_save();
    cJSON_Delete(root);

    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "status", "success");
    cJSON_AddStringToObject(response, "message", "Error handling configuration saved.");
    cJSON_AddBoolToObject(response, "restart_required", false);

    ret = send_json_response(req, response, 200);
    cJSON_Delete(response);
    return ret;
}

// GET /api/config/debug - Get debug configuration
static esp_err_t api_get_debug_handler(httpd_req_t *req) {
    debug_config_data_t debug;
    if (!config_manager_get_debug(&debug)) {
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
static esp_err_t api_post_debug_handler(httpd_req_t *req) {
    char buf[256];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_400(req);
        return ESP_FAIL;
    }

    debug_config_data_t debug;
    config_manager_get_debug(&debug);

    cJSON *item;
    if ((item = cJSON_GetObjectItem(root, "debug_enabled")) && cJSON_IsBool(item))
        debug.debug_enabled = cJSON_IsTrue(item);
    if ((item = cJSON_GetObjectItem(root, "stack_monitoring")) && cJSON_IsBool(item))
        debug.stack_monitoring = cJSON_IsTrue(item);
    if ((item = cJSON_GetObjectItem(root, "auto_reboot")) && cJSON_IsBool(item))
        debug.auto_reboot = cJSON_IsTrue(item);
    if ((item = cJSON_GetObjectItem(root, "i2s_reinit")) && cJSON_IsBool(item))
        debug.i2s_reinit = cJSON_IsTrue(item);
    if ((item = cJSON_GetObjectItem(root, "buffer_drain")) && cJSON_IsBool(item))
        debug.buffer_drain = cJSON_IsTrue(item);

    config_manager_set_debug(&debug);
    config_manager_save();
    cJSON_Delete(root);

    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "status", "success");
    cJSON_AddStringToObject(response, "message", "Debug configuration saved.");
    cJSON_AddBoolToObject(response, "restart_required", false);

    ret = send_json_response(req, response, 200);
    cJSON_Delete(response);
    return ret;
}

// GET /api/config/all - Get all configuration
static esp_err_t api_get_all_config_handler(httpd_req_t *req) {
    cJSON *root = cJSON_CreateObject();

    // WiFi
    wifi_config_data_t wifi;
    if (config_manager_get_wifi(&wifi)) {
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

    // TCP
    tcp_config_data_t tcp;
    if (config_manager_get_tcp(&tcp)) {
        cJSON *tcp_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(tcp_obj, "server_ip", tcp.server_ip);
        cJSON_AddNumberToObject(tcp_obj, "server_port", tcp.server_port);
        cJSON_AddItemToObject(root, "tcp", tcp_obj);
    }

    // Audio/I2S
    i2s_config_data_t i2s;
    if (config_manager_get_i2s(&i2s)) {
        cJSON *i2s_obj = cJSON_CreateObject();
        cJSON_AddNumberToObject(i2s_obj, "sample_rate", i2s.sample_rate);
        cJSON_AddNumberToObject(i2s_obj, "bits_per_sample", i2s.bits_per_sample);
        cJSON_AddNumberToObject(i2s_obj, "channels", i2s.channels);
        cJSON_AddNumberToObject(i2s_obj, "bck_pin", i2s.bck_pin);
        cJSON_AddNumberToObject(i2s_obj, "ws_pin", i2s.ws_pin);
        cJSON_AddNumberToObject(i2s_obj, "data_in_pin", i2s.data_in_pin);
        cJSON_AddItemToObject(root, "audio", i2s_obj);
    }

    // Buffer
    buffer_config_data_t buffer;
    if (config_manager_get_buffer(&buffer)) {
        cJSON *buf_obj = cJSON_CreateObject();
        cJSON_AddNumberToObject(buf_obj, "ring_buffer_size", buffer.ring_buffer_size);
        cJSON_AddNumberToObject(buf_obj, "dma_buf_count", buffer.dma_buf_count);
        cJSON_AddNumberToObject(buf_obj, "dma_buf_len", buffer.dma_buf_len);
        cJSON_AddItemToObject(root, "buffer", buf_obj);
    }

    // Tasks
    task_config_data_t tasks;
    if (config_manager_get_tasks(&tasks)) {
        cJSON *task_obj = cJSON_CreateObject();
        cJSON_AddNumberToObject(task_obj, "i2s_reader_priority", tasks.i2s_reader_priority);
        cJSON_AddNumberToObject(task_obj, "tcp_sender_priority", tasks.tcp_sender_priority);
        cJSON_AddNumberToObject(task_obj, "watchdog_priority", tasks.watchdog_priority);
        cJSON_AddNumberToObject(task_obj, "web_server_priority", tasks.web_server_priority);
        cJSON_AddNumberToObject(task_obj, "i2s_reader_core", tasks.i2s_reader_core);
        cJSON_AddNumberToObject(task_obj, "tcp_sender_core", tasks.tcp_sender_core);
        cJSON_AddNumberToObject(task_obj, "watchdog_core", tasks.watchdog_core);
        cJSON_AddNumberToObject(task_obj, "web_server_core", tasks.web_server_core);
        cJSON_AddItemToObject(root, "tasks", task_obj);
    }

    // Error handling
    error_config_data_t error;
    if (config_manager_get_error(&error)) {
        cJSON *err_obj = cJSON_CreateObject();
        cJSON_AddNumberToObject(err_obj, "max_reconnect_attempts", error.max_reconnect_attempts);
        cJSON_AddNumberToObject(err_obj, "reconnect_backoff_ms", error.reconnect_backoff_ms);
        cJSON_AddNumberToObject(err_obj, "max_reconnect_backoff_ms", error.max_reconnect_backoff_ms);
        cJSON_AddNumberToObject(err_obj, "max_i2s_failures", error.max_i2s_failures);
        cJSON_AddNumberToObject(err_obj, "max_buffer_overflows", error.max_buffer_overflows);
        cJSON_AddNumberToObject(err_obj, "watchdog_timeout_sec", error.watchdog_timeout_sec);
        cJSON_AddNumberToObject(err_obj, "ntp_resync_interval_sec", error.ntp_resync_interval_sec);
        cJSON_AddItemToObject(root, "error", err_obj);
    }

    // Debug
    debug_config_data_t debug;
    if (config_manager_get_debug(&debug)) {
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

// POST /api/system/save - Save configuration
static esp_err_t api_post_save_handler(httpd_req_t *req) {
    if (!config_manager_save()) {
        cJSON *response = cJSON_CreateObject();
        cJSON_AddStringToObject(response, "status", "error");
        cJSON_AddStringToObject(response, "message", "Failed to save configuration");
        
        send_json_response(req, response, 500);
        cJSON_Delete(response);
        return ESP_FAIL;
    }

    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "status", "success");
    cJSON_AddStringToObject(response, "message", "Configuration saved successfully");

    esp_err_t ret = send_json_response(req, response, 200);
    cJSON_Delete(response);
    return ret;
}

// POST /api/system/load - Reload configuration
static esp_err_t api_post_load_handler(httpd_req_t *req) {
    if (!config_manager_load()) {
        cJSON *response = cJSON_CreateObject();
        cJSON_AddStringToObject(response, "status", "error");
        cJSON_AddStringToObject(response, "message", "Failed to load configuration");
        
        send_json_response(req, response, 500);
        cJSON_Delete(response);
        return ESP_FAIL;
    }

    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "status", "success");
    cJSON_AddStringToObject(response, "message", "Configuration reloaded. Restart may be required.");
    cJSON_AddBoolToObject(response, "restart_required", true);

    esp_err_t ret = send_json_response(req, response, 200);
    cJSON_Delete(response);
    return ret;
}

// Root handler - serves basic info
static esp_err_t root_handler(httpd_req_t *req) {
    extern const unsigned char index_html_start[] asm("_binary_index_html_start");
    extern const unsigned char index_html_end[] asm("_binary_index_html_end");
    const size_t index_html_size = (index_html_end - index_html_start);
    
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, (const char *)index_html_start, index_html_size);
    return ESP_OK;
}

// Config page handler
static esp_err_t config_page_handler(httpd_req_t *req) {
    extern const unsigned char config_html_start[] asm("_binary_config_html_start");
    extern const unsigned char config_html_end[] asm("_binary_config_html_end");
    const size_t config_html_size = (config_html_end - config_html_start);
    
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, (const char *)config_html_start, config_html_size);
    return ESP_OK;
}

// Monitor page handler
static esp_err_t monitor_page_handler(httpd_req_t *req) {
    extern const unsigned char monitor_html_start[] asm("_binary_monitor_html_start");
    extern const unsigned char monitor_html_end[] asm("_binary_monitor_html_end");
    const size_t monitor_html_size = (monitor_html_end - monitor_html_start);
    
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, (const char *)monitor_html_start, monitor_html_size);
    return ESP_OK;
}

// CSS handler
static esp_err_t css_handler(httpd_req_t *req) {
    extern const unsigned char style_css_start[] asm("_binary_style_css_start");
    extern const unsigned char style_css_end[] asm("_binary_style_css_end");
    const size_t style_css_size = (style_css_end - style_css_start);
    
    httpd_resp_set_type(req, "text/css");
    httpd_resp_send(req, (const char *)style_css_start, style_css_size);
    return ESP_OK;
}

// JavaScript handlers
static esp_err_t js_api_handler(httpd_req_t *req) {
    extern const unsigned char api_js_start[] asm("_binary_api_js_start");
    extern const unsigned char api_js_end[] asm("_binary_api_js_end");
    const size_t api_js_size = (api_js_end - api_js_start);
    
    httpd_resp_set_type(req, "application/javascript");
    httpd_resp_send(req, (const char *)api_js_start, api_js_size);
    return ESP_OK;
}

static esp_err_t js_utils_handler(httpd_req_t *req) {
    extern const unsigned char utils_js_start[] asm("_binary_utils_js_start");
    extern const unsigned char utils_js_end[] asm("_binary_utils_js_end");
    const size_t utils_js_size = (utils_js_end - utils_js_start);
    
    httpd_resp_set_type(req, "application/javascript");
    httpd_resp_send(req, (const char *)utils_js_start, utils_js_size);
    return ESP_OK;
}

static esp_err_t js_app_handler(httpd_req_t *req) {
    extern const unsigned char app_js_start[] asm("_binary_app_js_start");
    extern const unsigned char app_js_end[] asm("_binary_app_js_end");
    const size_t app_js_size = (app_js_end - app_js_start);
    
    httpd_resp_set_type(req, "application/javascript");
    httpd_resp_send(req, (const char *)app_js_start, app_js_size);
    return ESP_OK;
}

static esp_err_t js_config_handler(httpd_req_t *req) {
    extern const unsigned char config_js_start[] asm("_binary_config_js_start");
    extern const unsigned char config_js_end[] asm("_binary_config_js_end");
    const size_t config_js_size = (config_js_end - config_js_start);
    
    httpd_resp_set_type(req, "application/javascript");
    httpd_resp_send(req, (const char *)config_js_start, config_js_size);
    return ESP_OK;
}

static esp_err_t js_monitor_handler(httpd_req_t *req) {
    extern const unsigned char monitor_js_start[] asm("_binary_monitor_js_start");
    extern const unsigned char monitor_js_end[] asm("_binary_monitor_js_end");
    const size_t monitor_js_size = (monitor_js_end - monitor_js_start);
    
    httpd_resp_set_type(req, "application/javascript");
    httpd_resp_send(req, (const char *)monitor_js_start, monitor_js_size);
    return ESP_OK;
}

bool web_server_init(void) {
    if (server != NULL) {
        ESP_LOGW(TAG, "Web server already running");
        return true;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.max_open_sockets = 4;
    config.lru_purge_enable = true;
    config.stack_size = 8192;

    ESP_LOGI(TAG, "Starting web server on port %d", config.server_port);
    
    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start web server");
        return false;
    }

    // Register URI handlers
    httpd_uri_t root_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &root_uri);

    // Static pages
    httpd_uri_t config_page_uri = {
        .uri = "/config.html",
        .method = HTTP_GET,
        .handler = config_page_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &config_page_uri);

    httpd_uri_t monitor_page_uri = {
        .uri = "/monitor.html",
        .method = HTTP_GET,
        .handler = monitor_page_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &monitor_page_uri);

    // CSS
    httpd_uri_t css_uri = {
        .uri = "/css/style.css",
        .method = HTTP_GET,
        .handler = css_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &css_uri);

    // JavaScript files
    httpd_uri_t js_api_uri = {
        .uri = "/js/api.js",
        .method = HTTP_GET,
        .handler = js_api_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &js_api_uri);

    httpd_uri_t js_utils_uri = {
        .uri = "/js/utils.js",
        .method = HTTP_GET,
        .handler = js_utils_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &js_utils_uri);

    httpd_uri_t js_app_uri = {
        .uri = "/js/app.js",
        .method = HTTP_GET,
        .handler = js_app_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &js_app_uri);

    httpd_uri_t js_config_uri = {
        .uri = "/js/config.js",
        .method = HTTP_GET,
        .handler = js_config_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &js_config_uri);

    httpd_uri_t js_monitor_uri = {
        .uri = "/js/monitor.js",
        .method = HTTP_GET,
        .handler = js_monitor_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &js_monitor_uri);

    // Configuration endpoints
    httpd_uri_t api_get_wifi = {
        .uri = "/api/config/wifi",
        .method = HTTP_GET,
        .handler = api_get_wifi_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &api_get_wifi);

    httpd_uri_t api_post_wifi = {
        .uri = "/api/config/wifi",
        .method = HTTP_POST,
        .handler = api_post_wifi_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &api_post_wifi);

    httpd_uri_t api_get_tcp = {
        .uri = "/api/config/tcp",
        .method = HTTP_GET,
        .handler = api_get_tcp_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &api_get_tcp);

    httpd_uri_t api_post_tcp = {
        .uri = "/api/config/tcp",
        .method = HTTP_POST,
        .handler = api_post_tcp_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &api_post_tcp);

    // System endpoints
    httpd_uri_t api_get_status = {
        .uri = "/api/system/status",
        .method = HTTP_GET,
        .handler = api_get_status_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &api_get_status);

    httpd_uri_t api_get_info = {
        .uri = "/api/system/info",
        .method = HTTP_GET,
        .handler = api_get_info_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &api_get_info);

    httpd_uri_t api_post_restart = {
        .uri = "/api/system/restart",
        .method = HTTP_POST,
        .handler = api_post_restart_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &api_post_restart);

    httpd_uri_t api_post_factory_reset = {
        .uri = "/api/system/factory-reset",
        .method = HTTP_POST,
        .handler = api_post_factory_reset_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &api_post_factory_reset);

    // Audio/I2S configuration endpoints
    httpd_uri_t api_get_audio = {
        .uri = "/api/config/audio",
        .method = HTTP_GET,
        .handler = api_get_audio_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &api_get_audio);

    httpd_uri_t api_post_audio = {
        .uri = "/api/config/audio",
        .method = HTTP_POST,
        .handler = api_post_audio_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &api_post_audio);

    // Buffer configuration endpoints
    httpd_uri_t api_get_buffer = {
        .uri = "/api/config/buffer",
        .method = HTTP_GET,
        .handler = api_get_buffer_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &api_get_buffer);

    httpd_uri_t api_post_buffer = {
        .uri = "/api/config/buffer",
        .method = HTTP_POST,
        .handler = api_post_buffer_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &api_post_buffer);

    // Task configuration endpoints
    httpd_uri_t api_get_tasks = {
        .uri = "/api/config/tasks",
        .method = HTTP_GET,
        .handler = api_get_tasks_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &api_get_tasks);

    httpd_uri_t api_post_tasks = {
        .uri = "/api/config/tasks",
        .method = HTTP_POST,
        .handler = api_post_tasks_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &api_post_tasks);

    // Error handling configuration endpoints
    httpd_uri_t api_get_error = {
        .uri = "/api/config/error",
        .method = HTTP_GET,
        .handler = api_get_error_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &api_get_error);

    httpd_uri_t api_post_error = {
        .uri = "/api/config/error",
        .method = HTTP_POST,
        .handler = api_post_error_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &api_post_error);

    // Debug configuration endpoints
    httpd_uri_t api_get_debug = {
        .uri = "/api/config/debug",
        .method = HTTP_GET,
        .handler = api_get_debug_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &api_get_debug);

    httpd_uri_t api_post_debug = {
        .uri = "/api/config/debug",
        .method = HTTP_POST,
        .handler = api_post_debug_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &api_post_debug);

    // Get all configuration
    httpd_uri_t api_get_all_config = {
        .uri = "/api/config/all",
        .method = HTTP_GET,
        .handler = api_get_all_config_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &api_get_all_config);

    // System save/load endpoints
    httpd_uri_t api_post_save = {
        .uri = "/api/system/save",
        .method = HTTP_POST,
        .handler = api_post_save_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &api_post_save);

    httpd_uri_t api_post_load = {
        .uri = "/api/system/load",
        .method = HTTP_POST,
        .handler = api_post_load_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &api_post_load);

    ESP_LOGI(TAG, "Web server started successfully");
    return true;
}

bool web_server_is_running(void) {
    return (server != NULL);
}

void web_server_deinit(void) {
    if (server != NULL) {
        httpd_stop(server);
        server = NULL;
        ESP_LOGI(TAG, "Web server stopped");
    }
}

httpd_handle_t web_server_get_handle(void) {
    return server;
}
