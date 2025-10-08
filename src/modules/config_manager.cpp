#include "config_manager.h"
#include "../config.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>

static const char* TAG = "CONFIG_MANAGER";
static const char* NVS_NAMESPACE = "audio_stream";

static nvs_handle_t g_nvs_handle;
static bool initialized = false;

// In-memory configuration cache
static wifi_config_data_t wifi_config;
static tcp_config_data_t tcp_config;
static ntp_config_data_t ntp_config;
static i2s_config_data_t i2s_config;
static buffer_config_data_t buffer_config;
static task_config_data_t task_config;
static error_config_data_t error_config;
static debug_config_data_t debug_config;
static auth_config_data_t auth_config;

// Load defaults from config.h
static void load_defaults(void) {
    // WiFi defaults
    strncpy(wifi_config.ssid, WIFI_SSID, sizeof(wifi_config.ssid) - 1);
    strncpy(wifi_config.password, WIFI_PASSWORD, sizeof(wifi_config.password) - 1);
    wifi_config.use_static_ip = (strcmp(STATIC_IP_ADDR, "0.0.0.0") != 0);
    strncpy(wifi_config.static_ip, STATIC_IP_ADDR, sizeof(wifi_config.static_ip) - 1);
    strncpy(wifi_config.gateway, GATEWAY_ADDR, sizeof(wifi_config.gateway) - 1);
    strncpy(wifi_config.subnet, SUBNET_MASK, sizeof(wifi_config.subnet) - 1);
    strncpy(wifi_config.dns_primary, PRIMARY_DNS, sizeof(wifi_config.dns_primary) - 1);
    strncpy(wifi_config.dns_secondary, SECONDARY_DNS, sizeof(wifi_config.dns_secondary) - 1);

    // TCP defaults
    strncpy(tcp_config.server_ip, TCP_SERVER_IP, sizeof(tcp_config.server_ip) - 1);
    tcp_config.server_port = TCP_SERVER_PORT;

    // NTP defaults
    strncpy(ntp_config.ntp_server, NTP_SERVER, sizeof(ntp_config.ntp_server) - 1);
    strncpy(ntp_config.timezone, NTP_TIMEZONE, sizeof(ntp_config.timezone) - 1);

    // I2S defaults
    i2s_config.sample_rate = SAMPLE_RATE;
    i2s_config.bits_per_sample = BITS_PER_SAMPLE;
    i2s_config.channels = CHANNELS;
    i2s_config.bck_pin = I2S_BCK_PIN;
    i2s_config.ws_pin = I2S_WS_PIN;
    i2s_config.data_in_pin = I2S_DATA_IN_PIN;

    // Buffer defaults
    buffer_config.ring_buffer_size = RING_BUFFER_SIZE;
    buffer_config.dma_buf_count = I2S_DMA_BUF_COUNT;
    buffer_config.dma_buf_len = I2S_DMA_BUF_LEN;

    // Task defaults
    task_config.i2s_reader_priority = I2S_READER_PRIORITY;
    task_config.tcp_sender_priority = TCP_SENDER_PRIORITY;
    task_config.watchdog_priority = WATCHDOG_PRIORITY;
    task_config.web_server_priority = 5; // Medium priority
    task_config.i2s_reader_core = I2S_READER_CORE;
    task_config.tcp_sender_core = TCP_SENDER_CORE;
    task_config.watchdog_core = WATCHDOG_CORE;
    task_config.web_server_core = 0; // Core 0 with WiFi

    // Error handling defaults
    error_config.max_reconnect_attempts = MAX_RECONNECT_ATTEMPTS;
    error_config.reconnect_backoff_ms = RECONNECT_BACKOFF_MS;
    error_config.max_reconnect_backoff_ms = MAX_RECONNECT_BACKOFF_MS;
    error_config.max_i2s_failures = MAX_I2S_FAILURES;
    error_config.max_buffer_overflows = MAX_BUFFER_OVERFLOWS;
    error_config.watchdog_timeout_sec = WATCHDOG_TIMEOUT_SEC;
    error_config.ntp_resync_interval_sec = NTP_RESYNC_INTERVAL_SEC;

    // Debug defaults
    debug_config.debug_enabled = DEBUG_ENABLED;
    debug_config.stack_monitoring = ENABLE_STACK_MONITORING;
    debug_config.auto_reboot = ENABLE_AUTO_REBOOT;
    debug_config.i2s_reinit = ENABLE_I2S_REINIT;
    debug_config.buffer_drain = ENABLE_BUFFER_DRAIN;

    // Auth defaults
    strncpy(auth_config.username, WEB_AUTH_USERNAME, sizeof(auth_config.username) - 1);
    strncpy(auth_config.password, WEB_AUTH_PASSWORD, sizeof(auth_config.password) - 1);

    ESP_LOGI(TAG, "Loaded default configuration from config.h");
}

bool config_manager_init(void) {
    if (initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return true;
    }

    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &g_nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS handle: %s", esp_err_to_name(err));
        return false;
    }

    initialized = true;
    ESP_LOGI(TAG, "Config manager initialized");
    return true;
}

bool config_manager_is_first_boot(void) {
    if (!initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return true;
    }

    uint32_t version = 0;
    esp_err_t err = nvs_get_u32(g_nvs_handle, "version", &version);
    return (err == ESP_ERR_NVS_NOT_FOUND);
}

bool config_manager_load(void) {
    if (!initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return false;
    }

    bool is_first_boot = config_manager_is_first_boot();
    
    if (is_first_boot) {
        ESP_LOGI(TAG, "First boot detected, loading defaults");
        load_defaults();
        
        // Save version marker
        nvs_set_u32(g_nvs_handle, "version", CONFIG_VERSION);
        nvs_commit(g_nvs_handle);
        
        return true;
    }

    // Load WiFi config
    size_t required_size;
    esp_err_t err;

    required_size = sizeof(wifi_config);
    err = nvs_get_blob(g_nvs_handle, "wifi", &wifi_config, &required_size);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Error reading wifi config: %s", esp_err_to_name(err));
    }

    required_size = sizeof(tcp_config);
    err = nvs_get_blob(g_nvs_handle, "tcp", &tcp_config, &required_size);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Error reading tcp config: %s", esp_err_to_name(err));
    }

    required_size = sizeof(ntp_config);
    err = nvs_get_blob(g_nvs_handle, "ntp", &ntp_config, &required_size);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Error reading ntp config: %s", esp_err_to_name(err));
    }

    required_size = sizeof(i2s_config);
    err = nvs_get_blob(g_nvs_handle, "i2s", &i2s_config, &required_size);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Error reading i2s config: %s", esp_err_to_name(err));
    }

    required_size = sizeof(buffer_config);
    err = nvs_get_blob(g_nvs_handle, "buffer", &buffer_config, &required_size);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Error reading buffer config: %s", esp_err_to_name(err));
    }

    required_size = sizeof(task_config);
    err = nvs_get_blob(g_nvs_handle, "tasks", &task_config, &required_size);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Error reading task config: %s", esp_err_to_name(err));
    }

    required_size = sizeof(error_config);
    err = nvs_get_blob(g_nvs_handle, "error", &error_config, &required_size);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Error reading error config: %s", esp_err_to_name(err));
    }

    required_size = sizeof(debug_config);
    err = nvs_get_blob(g_nvs_handle, "debug", &debug_config, &required_size);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Error reading debug config: %s", esp_err_to_name(err));
    }

    ESP_LOGI(TAG, "Configuration loaded from NVS");
    return true;
}

bool config_manager_save(void) {
    if (!initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return false;
    }

    esp_err_t err;

    err = nvs_set_blob(g_nvs_handle, "wifi", &wifi_config, sizeof(wifi_config));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error saving wifi config: %s", esp_err_to_name(err));
        return false;
    }

    err = nvs_set_blob(g_nvs_handle, "tcp", &tcp_config, sizeof(tcp_config));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error saving tcp config: %s", esp_err_to_name(err));
        return false;
    }

    err = nvs_set_blob(g_nvs_handle, "ntp", &ntp_config, sizeof(ntp_config));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error saving ntp config: %s", esp_err_to_name(err));
        return false;
    }

    err = nvs_set_blob(g_nvs_handle, "i2s", &i2s_config, sizeof(i2s_config));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error saving i2s config: %s", esp_err_to_name(err));
        return false;
    }

    err = nvs_set_blob(g_nvs_handle, "buffer", &buffer_config, sizeof(buffer_config));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error saving buffer config: %s", esp_err_to_name(err));
        return false;
    }

    err = nvs_set_blob(g_nvs_handle, "tasks", &task_config, sizeof(task_config));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error saving task config: %s", esp_err_to_name(err));
        return false;
    }

    err = nvs_set_blob(g_nvs_handle, "error", &error_config, sizeof(error_config));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error saving error config: %s", esp_err_to_name(err));
        return false;
    }

    err = nvs_set_blob(g_nvs_handle, "debug", &debug_config, sizeof(debug_config));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error saving debug config: %s", esp_err_to_name(err));
        return false;
    }

    err = nvs_commit(g_nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error committing NVS: %s", esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG, "Configuration saved to NVS");
    return true;
}

bool config_manager_reset_to_factory(void) {
    if (!initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return false;
    }

    ESP_LOGI(TAG, "Resetting to factory defaults");
    
    // Erase all keys
    esp_err_t err = nvs_erase_all(g_nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error erasing NVS: %s", esp_err_to_name(err));
        return false;
    }

    // Load and save defaults
    load_defaults();
    nvs_set_u32(g_nvs_handle, "version", CONFIG_VERSION);
    
    return config_manager_save();
}

// Getter/Setter implementations
bool config_manager_get_wifi(wifi_config_data_t* config) {
    if (!initialized || !config) return false;
    memcpy(config, &wifi_config, sizeof(wifi_config_data_t));
    return true;
}

bool config_manager_set_wifi(const wifi_config_data_t* config) {
    if (!initialized || !config) return false;
    memcpy(&wifi_config, config, sizeof(wifi_config_data_t));
    return true;
}

bool config_manager_get_tcp(tcp_config_data_t* config) {
    if (!initialized || !config) return false;
    memcpy(config, &tcp_config, sizeof(tcp_config_data_t));
    return true;
}

bool config_manager_set_tcp(const tcp_config_data_t* config) {
    if (!initialized || !config) return false;
    memcpy(&tcp_config, config, sizeof(tcp_config_data_t));
    return true;
}

bool config_manager_get_ntp(ntp_config_data_t* config) {
    if (!initialized || !config) return false;
    memcpy(config, &ntp_config, sizeof(ntp_config_data_t));
    return true;
}

bool config_manager_set_ntp(const ntp_config_data_t* config) {
    if (!initialized || !config) return false;
    memcpy(&ntp_config, config, sizeof(ntp_config_data_t));
    return true;
}

bool config_manager_get_i2s(i2s_config_data_t* config) {
    if (!initialized || !config) return false;
    memcpy(config, &i2s_config, sizeof(i2s_config_data_t));
    return true;
}

bool config_manager_set_i2s(const i2s_config_data_t* config) {
    if (!initialized || !config) return false;
    memcpy(&i2s_config, config, sizeof(i2s_config_data_t));
    return true;
}

bool config_manager_get_buffer(buffer_config_data_t* config) {
    if (!initialized || !config) return false;
    memcpy(config, &buffer_config, sizeof(buffer_config_data_t));
    return true;
}

bool config_manager_set_buffer(const buffer_config_data_t* config) {
    if (!initialized || !config) return false;
    memcpy(&buffer_config, config, sizeof(buffer_config_data_t));
    return true;
}

bool config_manager_get_tasks(task_config_data_t* config) {
    if (!initialized || !config) return false;
    memcpy(config, &task_config, sizeof(task_config_data_t));
    return true;
}

bool config_manager_set_tasks(const task_config_data_t* config) {
    if (!initialized || !config) return false;
    memcpy(&task_config, config, sizeof(task_config_data_t));
    return true;
}

bool config_manager_get_error(error_config_data_t* config) {
    if (!initialized || !config) return false;
    memcpy(config, &error_config, sizeof(error_config_data_t));
    return true;
}

bool config_manager_set_error(const error_config_data_t* config) {
    if (!initialized || !config) return false;
    memcpy(&error_config, config, sizeof(error_config_data_t));
    return true;
}

bool config_manager_get_debug(debug_config_data_t* config) {
    if (!initialized || !config) return false;
    memcpy(config, &debug_config, sizeof(debug_config_data_t));
    return true;
}

bool config_manager_set_debug(const debug_config_data_t* config) {
    if (!initialized || !config) return false;
    memcpy(&debug_config, config, sizeof(debug_config_data_t));
    return true;
}

bool config_manager_get_auth(auth_config_data_t* config) {
    if (!initialized || !config) return false;
    memcpy(config, &auth_config, sizeof(auth_config_data_t));
    return true;
}

bool config_manager_set_auth(const auth_config_data_t* config) {
    if (!initialized || !config) return false;
    memcpy(&auth_config, config, sizeof(auth_config_data_t));
    return true;
}

void config_manager_deinit(void) {
    if (initialized) {
        nvs_close(g_nvs_handle);
        initialized = false;
        ESP_LOGI(TAG, "Config manager deinitialized");
    }
}
