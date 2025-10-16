#include "config_manager.h"
#include "../config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>

static const char *TAG = "CONFIG_MANAGER";
static const char *NVS_NAMESPACE = "audio_stream";

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
static streaming_config_data_t streaming_config;
static adaptive_buffer_config_data_t adaptive_buffer_config;
static udp_config_data_t udp_config;
static tcp_optimization_config_data_t tcp_optimization_config;
static performance_monitor_config_data_t performance_monitor_config;

// Load defaults from config.h
static void load_defaults(void)
{
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
    i2s_config.sample_rate = AUDIO_SAMPLE_RATE_DEFAULT;
    i2s_config.bits_per_sample = BITS_PER_SAMPLE; // 16-bit default
    i2s_config.channels = CHANNELS; // Mono default
    i2s_config.bck_pin = I2S_BCLK_GPIO;
    i2s_config.ws_pin = I2S_WS_GPIO;
    i2s_config.data_in_pin = I2S_SD_GPIO;
    i2s_config.audio_format = AUDIO_FORMAT_DEFAULT;
    i2s_config.auto_gain_control = false;
    i2s_config.input_gain_db = 0; // 0 dB gain

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

    // Streaming defaults
    streaming_config.protocol = STREAMING_PROTOCOL;

    // Adaptive buffer defaults
    adaptive_buffer_config.min_size = ADAPTIVE_BUFFER_MIN_SIZE;
    adaptive_buffer_config.max_size = ADAPTIVE_BUFFER_MAX_SIZE;
    adaptive_buffer_config.default_size = ADAPTIVE_BUFFER_DEFAULT_SIZE;
    adaptive_buffer_config.threshold_low = ADAPTIVE_THRESHOLD_LOW;
    adaptive_buffer_config.threshold_high = ADAPTIVE_THRESHOLD_HIGH;
    adaptive_buffer_config.check_interval_ms = ADAPTIVE_CHECK_INTERVAL_MS;
    adaptive_buffer_config.resize_delay_ms = ADAPTIVE_RESIZE_DELAY_MS;

    // UDP defaults
    udp_config.packet_max_size = UDP_PACKET_MAX_SIZE;
    udp_config.send_timeout_ms = UDP_SEND_TIMEOUT_MS;
    udp_config.buffer_count = UDP_BUFFER_COUNT;
    strncpy(udp_config.multicast_group, "239.255.0.1", sizeof(udp_config.multicast_group) - 1);
    udp_config.multicast_port = 9002;

    // TCP optimization defaults
    tcp_optimization_config.enabled = TCP_KEEPALIVE_ENABLED;
    tcp_optimization_config.keepalive_idle_sec = TCP_KEEPALIVE_IDLE_SEC;
    tcp_optimization_config.keepalive_interval_sec = TCP_KEEPALIVE_INTERVAL_SEC;
    tcp_optimization_config.keepalive_count = TCP_KEEPALIVE_COUNT;
    tcp_optimization_config.nodelay_enabled = TCP_NODELAY_ENABLED;
    tcp_optimization_config.tx_buffer_size = TCP_TX_BUFFER_SIZE;
    tcp_optimization_config.rx_buffer_size = TCP_RX_BUFFER_SIZE;

    // Performance monitor defaults
    performance_monitor_config.interval_ms = HISTORY_INTERVAL_MS;
    performance_monitor_config.max_entries = MAX_HISTORY_ENTRIES;

    ESP_LOGI(TAG, "Loaded default configuration from config.h");
}

bool config_manager_init(void)
{
    if (initialized)
    {
        ESP_LOGW(TAG, "Already initialized");
        return true;
    }

    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &g_nvs_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Error opening NVS handle: %s", esp_err_to_name(err));
        return false;
    }

    initialized = true;
    ESP_LOGI(TAG, "Config manager initialized");
    return true;
}

bool config_manager_is_first_boot(void)
{
    if (!initialized)
    {
        ESP_LOGE(TAG, "Not initialized");
        return true;
    }

    uint32_t version = 0;
    esp_err_t err = nvs_get_u32(g_nvs_handle, "version", &version);
    return (err == ESP_ERR_NVS_NOT_FOUND);
}

bool config_manager_load(void)
{
    if (!initialized)
    {
        ESP_LOGE(TAG, "Not initialized");
        return false;
    }

    bool is_first_boot = config_manager_is_first_boot();

    // ALWAYS load auth defaults from config.h (not stored in NVS)
    strncpy(auth_config.username, WEB_AUTH_USERNAME, sizeof(auth_config.username) - 1);
    strncpy(auth_config.password, WEB_AUTH_PASSWORD, sizeof(auth_config.password) - 1);
    auth_config.username[sizeof(auth_config.username) - 1] = '\0';
    auth_config.password[sizeof(auth_config.password) - 1] = '\0';
    ESP_LOGI(TAG, "Loaded auth credentials from config.h");

    if (is_first_boot)
    {
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
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGW(TAG, "Error reading wifi config: %s", esp_err_to_name(err));
    }

    required_size = sizeof(tcp_config);
    err = nvs_get_blob(g_nvs_handle, "tcp", &tcp_config, &required_size);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGW(TAG, "Error reading tcp config: %s", esp_err_to_name(err));
    }

    required_size = sizeof(ntp_config);
    err = nvs_get_blob(g_nvs_handle, "ntp", &ntp_config, &required_size);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGW(TAG, "Error reading ntp config: %s", esp_err_to_name(err));
    }

    required_size = sizeof(i2s_config);
    err = nvs_get_blob(g_nvs_handle, "i2s", &i2s_config, &required_size);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGW(TAG, "Error reading i2s config: %s", esp_err_to_name(err));
    }

    required_size = sizeof(buffer_config);
    err = nvs_get_blob(g_nvs_handle, "buffer", &buffer_config, &required_size);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGW(TAG, "Error reading buffer config: %s", esp_err_to_name(err));
    }

    required_size = sizeof(task_config);
    err = nvs_get_blob(g_nvs_handle, "tasks", &task_config, &required_size);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGW(TAG, "Error reading task config: %s", esp_err_to_name(err));
    }

    required_size = sizeof(error_config);
    err = nvs_get_blob(g_nvs_handle, "error", &error_config, &required_size);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGW(TAG, "Error reading error config: %s", esp_err_to_name(err));
    }

    required_size = sizeof(debug_config);
    err = nvs_get_blob(g_nvs_handle, "debug", &debug_config, &required_size);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGW(TAG, "Error reading debug config: %s", esp_err_to_name(err));
    }

    required_size = sizeof(streaming_config);
    err = nvs_get_blob(g_nvs_handle, "streaming", &streaming_config, &required_size);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGW(TAG, "Error reading streaming config: %s", esp_err_to_name(err));
    }

    required_size = sizeof(adaptive_buffer_config);
    err = nvs_get_blob(g_nvs_handle, "adaptive_buffer", &adaptive_buffer_config, &required_size);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGW(TAG, "Error reading adaptive buffer config: %s", esp_err_to_name(err));
    }

    required_size = sizeof(udp_config);
    err = nvs_get_blob(g_nvs_handle, "udp", &udp_config, &required_size);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGW(TAG, "Error reading udp config: %s", esp_err_to_name(err));
    }

    required_size = sizeof(tcp_optimization_config);
    err = nvs_get_blob(g_nvs_handle, "tcp_optimization", &tcp_optimization_config, &required_size);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGW(TAG, "Error reading tcp optimization config: %s", esp_err_to_name(err));
    }

    required_size = sizeof(performance_monitor_config);
    err = nvs_get_blob(g_nvs_handle, "performance_monitor", &performance_monitor_config, &required_size);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGW(TAG, "Error reading performance monitor config: %s", esp_err_to_name(err));
    }

    ESP_LOGI(TAG, "Configuration loaded from NVS");
    return true;
}

bool config_manager_save(void)
{
    if (!initialized)
    {
        ESP_LOGE(TAG, "Not initialized");
        return false;
    }

    esp_err_t err;

    err = nvs_set_blob(g_nvs_handle, "wifi", &wifi_config, sizeof(wifi_config));
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Error saving wifi config: %s", esp_err_to_name(err));
        return false;
    }

    err = nvs_set_blob(g_nvs_handle, "tcp", &tcp_config, sizeof(tcp_config));
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Error saving tcp config: %s", esp_err_to_name(err));
        return false;
    }

    err = nvs_set_blob(g_nvs_handle, "ntp", &ntp_config, sizeof(ntp_config));
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Error saving ntp config: %s", esp_err_to_name(err));
        return false;
    }

    err = nvs_set_blob(g_nvs_handle, "i2s", &i2s_config, sizeof(i2s_config));
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Error saving i2s config: %s", esp_err_to_name(err));
        return false;
    }

    err = nvs_set_blob(g_nvs_handle, "buffer", &buffer_config, sizeof(buffer_config));
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Error saving buffer config: %s", esp_err_to_name(err));
        return false;
    }

    err = nvs_set_blob(g_nvs_handle, "tasks", &task_config, sizeof(task_config));
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Error saving task config: %s", esp_err_to_name(err));
        return false;
    }

    err = nvs_set_blob(g_nvs_handle, "error", &error_config, sizeof(error_config));
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Error saving error config: %s", esp_err_to_name(err));
        return false;
    }

    err = nvs_set_blob(g_nvs_handle, "debug", &debug_config, sizeof(debug_config));
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Error saving debug config: %s", esp_err_to_name(err));
        return false;
    }

    err = nvs_set_blob(g_nvs_handle, "streaming", &streaming_config, sizeof(streaming_config));
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Error saving streaming config: %s", esp_err_to_name(err));
        return false;
    }

    err = nvs_set_blob(g_nvs_handle, "adaptive_buffer", &adaptive_buffer_config, sizeof(adaptive_buffer_config));
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Error saving adaptive buffer config: %s", esp_err_to_name(err));
        return false;
    }

    err = nvs_set_blob(g_nvs_handle, "udp", &udp_config, sizeof(udp_config));
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Error saving udp config: %s", esp_err_to_name(err));
        return false;
    }

    err = nvs_set_blob(g_nvs_handle, "tcp_optimization", &tcp_optimization_config, sizeof(tcp_optimization_config));
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Error saving tcp optimization config: %s", esp_err_to_name(err));
        return false;
    }

    err = nvs_set_blob(g_nvs_handle, "performance_monitor", &performance_monitor_config, sizeof(performance_monitor_config));
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Error saving performance monitor config: %s", esp_err_to_name(err));
        return false;
    }

    err = nvs_commit(g_nvs_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Error committing NVS: %s", esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG, "Configuration saved to NVS");
    return true;
}

bool config_manager_reset_to_factory(void)
{
    if (!initialized)
    {
        ESP_LOGE(TAG, "Not initialized");
        return false;
    }

    ESP_LOGI(TAG, "Resetting to factory defaults");

    // Erase all keys
    esp_err_t err = nvs_erase_all(g_nvs_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Error erasing NVS: %s", esp_err_to_name(err));
        return false;
    }

    // Load and save defaults
    load_defaults();
    nvs_set_u32(g_nvs_handle, "version", CONFIG_VERSION);

    return config_manager_save();
}

// Getter/Setter implementations
bool config_manager_get_wifi(wifi_config_data_t *config)
{
    if (!initialized || !config)
        return false;
    memcpy(config, &wifi_config, sizeof(wifi_config_data_t));
    return true;
}

bool config_manager_set_wifi(const wifi_config_data_t *config)
{
    if (!initialized || !config)
        return false;
    memcpy(&wifi_config, config, sizeof(wifi_config_data_t));
    return true;
}

bool config_manager_get_tcp(tcp_config_data_t *config)
{
    if (!initialized || !config)
        return false;
    memcpy(config, &tcp_config, sizeof(tcp_config_data_t));
    return true;
}

bool config_manager_set_tcp(const tcp_config_data_t *config)
{
    if (!initialized || !config)
        return false;
    memcpy(&tcp_config, config, sizeof(tcp_config_data_t));
    return true;
}

bool config_manager_get_ntp(ntp_config_data_t *config)
{
    if (!initialized || !config)
        return false;
    memcpy(config, &ntp_config, sizeof(ntp_config_data_t));
    return true;
}

bool config_manager_set_ntp(const ntp_config_data_t *config)
{
    if (!initialized || !config)
        return false;
    memcpy(&ntp_config, config, sizeof(ntp_config_data_t));
    return true;
}

bool config_manager_get_i2s(i2s_config_data_t *config)
{
    if (!initialized || !config)
        return false;
    memcpy(config, &i2s_config, sizeof(i2s_config_data_t));
    return true;
}

bool config_manager_set_i2s(const i2s_config_data_t *config)
{
    if (!initialized || !config)
        return false;
    memcpy(&i2s_config, config, sizeof(i2s_config_data_t));
    return true;
}

bool config_manager_get_buffer(buffer_config_data_t *config)
{
    if (!initialized || !config)
        return false;
    memcpy(config, &buffer_config, sizeof(buffer_config_data_t));
    return true;
}

bool config_manager_set_buffer(const buffer_config_data_t *config)
{
    if (!initialized || !config)
        return false;
    memcpy(&buffer_config, config, sizeof(buffer_config_data_t));
    return true;
}

bool config_manager_get_tasks(task_config_data_t *config)
{
    if (!initialized || !config)
        return false;
    memcpy(config, &task_config, sizeof(task_config_data_t));
    return true;
}

bool config_manager_set_tasks(const task_config_data_t *config)
{
    if (!initialized || !config)
        return false;
    memcpy(&task_config, config, sizeof(task_config_data_t));
    return true;
}

bool config_manager_get_error(error_config_data_t *config)
{
    if (!initialized || !config)
        return false;
    memcpy(config, &error_config, sizeof(error_config_data_t));
    return true;
}

bool config_manager_set_error(const error_config_data_t *config)
{
    if (!initialized || !config)
        return false;
    memcpy(&error_config, config, sizeof(error_config_data_t));
    return true;
}

bool config_manager_get_debug(debug_config_data_t *config)
{
    if (!initialized || !config)
        return false;
    memcpy(config, &debug_config, sizeof(debug_config_data_t));
    return true;
}

bool config_manager_set_debug(const debug_config_data_t *config)
{
    if (!initialized || !config)
        return false;
    memcpy(&debug_config, config, sizeof(debug_config_data_t));
    return true;
}

bool config_manager_get_auth(auth_config_data_t *config)
{
    if (!initialized || !config)
        return false;
    memcpy(config, &auth_config, sizeof(auth_config_data_t));
    return true;
}

bool config_manager_set_auth(const auth_config_data_t *config)
{
    if (!initialized || !config)
        return false;
    memcpy(&auth_config, config, sizeof(auth_config_data_t));
    return true;
}

bool config_manager_get_streaming(streaming_config_data_t *config)
{
    if (!initialized || !config)
        return false;
    memcpy(config, &streaming_config, sizeof(streaming_config_data_t));
    return true;
}

bool config_manager_set_streaming(const streaming_config_data_t *config)
{
    if (!initialized || !config)
        return false;
    memcpy(&streaming_config, config, sizeof(streaming_config_data_t));
    return true;
}

bool config_manager_get_adaptive_buffer(adaptive_buffer_config_data_t *config)
{
    if (!initialized || !config)
        return false;
    memcpy(config, &adaptive_buffer_config, sizeof(adaptive_buffer_config_data_t));
    return true;
}

bool config_manager_set_adaptive_buffer(const adaptive_buffer_config_data_t *config)
{
    if (!initialized || !config)
        return false;
    memcpy(&adaptive_buffer_config, config, sizeof(adaptive_buffer_config_data_t));
    return true;
}

bool config_manager_get_udp(udp_config_data_t *config)
{
    if (!initialized || !config)
        return false;
    memcpy(config, &udp_config, sizeof(udp_config_data_t));
    return true;
}

bool config_manager_set_udp(const udp_config_data_t *config)
{
    if (!initialized || !config)
        return false;
    memcpy(&udp_config, config, sizeof(udp_config_data_t));
    return true;
}

bool config_manager_get_tcp_optimization(tcp_optimization_config_data_t *config)
{
    if (!initialized || !config)
        return false;
    memcpy(config, &tcp_optimization_config, sizeof(tcp_optimization_config_data_t));
    return true;
}

bool config_manager_set_tcp_optimization(const tcp_optimization_config_data_t *config)
{
    if (!initialized || !config)
        return false;
    memcpy(&tcp_optimization_config, config, sizeof(tcp_optimization_config_data_t));
    return true;
}

bool config_manager_get_performance_monitor(performance_monitor_config_data_t *config)
{
    if (!initialized || !config)
        return false;
    memcpy(config, &performance_monitor_config, sizeof(performance_monitor_config_data_t));
    return true;
}

bool config_manager_set_performance_monitor(const performance_monitor_config_data_t *config)
{
    if (!initialized || !config)
        return false;
    memcpy(&performance_monitor_config, config, sizeof(performance_monitor_config_data_t));
    return true;
}

uint8_t config_manager_get_version(void)
{
    return CONFIG_VERSION;
}

bool config_manager_needs_migration(uint8_t from_version)
{
    return from_version < CONFIG_VERSION;
}

bool config_manager_migrate(uint8_t from_version)
{
    ESP_LOGI(TAG, "Migrating configuration from version %d to %d", from_version, CONFIG_VERSION);

    // For now, no migration needed since we're at version 1
    // Future versions will have migration logic here
    return true;
}

bool config_manager_validate(void)
{
    // Validate WiFi configuration
    if (strlen(wifi_config.ssid) == 0 || strlen(wifi_config.ssid) > 31) {
        ESP_LOGE(TAG, "Invalid WiFi SSID length");
        return false;
    }

    // Validate TCP configuration
    if (tcp_config.server_port == 0) {
        ESP_LOGE(TAG, "Invalid TCP server port: %d", tcp_config.server_port);
        return false;
    }

    // Validate I2S configuration using the new audio format validation
    if (!config_manager_validate_audio_format(i2s_config.sample_rate, i2s_config.bits_per_sample, i2s_config.channels)) {
        return false;
    }

    // Validate buffer configuration
    if (buffer_config.ring_buffer_size < 1024 || buffer_config.ring_buffer_size > 1024*1024) {
        ESP_LOGE(TAG, "Invalid ring buffer size: %lu", buffer_config.ring_buffer_size);
        return false;
    }

    ESP_LOGI(TAG, "Configuration validation passed");
    return true;
}

bool config_manager_export_json(char* json_output, size_t buffer_size)
{
    if (!initialized || !json_output || buffer_size < 4096) {
        return false;
    }

    int offset = 0;
    offset += snprintf(json_output + offset, buffer_size - offset,
        "{\n"
        "  \"version\": %d,\n"
        "  \"timestamp\": %lld,\n"
        "  \"wifi\": {\n"
        "    \"ssid\": \"%s\",\n"
        "    \"password\": \"%s\",\n"
        "    \"use_static_ip\": %s,\n"
        "    \"static_ip\": \"%s\",\n"
        "    \"gateway\": \"%s\",\n"
        "    \"subnet\": \"%s\",\n"
        "    \"dns_primary\": \"%s\",\n"
        "    \"dns_secondary\": \"%s\"\n"
        "  },\n",
        CONFIG_VERSION,
        esp_timer_get_time() / 1000,
        wifi_config.ssid,
        wifi_config.password,
        wifi_config.use_static_ip ? "true" : "false",
        wifi_config.static_ip,
        wifi_config.gateway,
        wifi_config.subnet,
        wifi_config.dns_primary,
        wifi_config.dns_secondary
    );

    offset += snprintf(json_output + offset, buffer_size - offset,
        "  \"tcp\": {\n"
        "    \"server_ip\": \"%s\",\n"
        "    \"server_port\": %u\n"
        "  },\n",
        tcp_config.server_ip,
        tcp_config.server_port
    );

    offset += snprintf(json_output + offset, buffer_size - offset,
        "  \"ntp\": {\n"
        "    \"server\": \"%s\",\n"
        "    \"timezone\": \"%s\"\n"
        "  },\n",
        ntp_config.ntp_server,
        ntp_config.timezone
    );

    char format_name[16], sample_rate_name[16];
    config_manager_get_audio_format_name(i2s_config.audio_format, format_name, sizeof(format_name));
    config_manager_get_sample_rate_name(i2s_config.sample_rate, sample_rate_name, sizeof(sample_rate_name));

    offset += snprintf(json_output + offset, buffer_size - offset,
        "  \"i2s\": {\n"
        "    \"sample_rate\": %lu,\n"
        "    \"sample_rate_name\": \"%s\",\n"
        "    \"bits_per_sample\": %u,\n"
        "    \"audio_format\": %u,\n"
        "    \"format_name\": \"%s\",\n"
        "    \"channels\": %u,\n"
        "    \"bck_pin\": %u,\n"
        "    \"ws_pin\": %u,\n"
        "    \"data_in_pin\": %u,\n"
        "    \"auto_gain_control\": %s,\n"
        "    \"input_gain_db\": %u,\n"
        "    \"data_rate_bps\": %lu\n"
        "  },\n",
        i2s_config.sample_rate,
        sample_rate_name,
        i2s_config.bits_per_sample,
        i2s_config.audio_format,
        format_name,
        i2s_config.channels,
        i2s_config.bck_pin,
        i2s_config.ws_pin,
        i2s_config.data_in_pin,
        i2s_config.auto_gain_control ? "true" : "false",
        i2s_config.input_gain_db,
        config_manager_calculate_audio_data_rate(i2s_config.sample_rate, i2s_config.bits_per_sample, i2s_config.channels)
    );

    offset += snprintf(json_output + offset, buffer_size - offset,
        "  \"buffer\": {\n"
        "    \"ring_buffer_size\": %lu,\n"
        "    \"dma_buf_count\": %u,\n"
        "    \"dma_buf_len\": %u\n"
        "  },\n",
        buffer_config.ring_buffer_size,
        buffer_config.dma_buf_count,
        buffer_config.dma_buf_len
    );

    offset += snprintf(json_output + offset, buffer_size - offset,
        "  \"streaming\": {\n"
        "    \"protocol\": %u\n"
        "  },\n",
        streaming_config.protocol
    );

    offset += snprintf(json_output + offset, buffer_size - offset,
        "  \"adaptive_buffer\": {\n"
        "    \"min_size\": %lu,\n"
        "    \"max_size\": %lu,\n"
        "    \"default_size\": %lu,\n"
        "    \"threshold_low\": %u,\n"
        "    \"threshold_high\": %u,\n"
        "    \"check_interval_ms\": %lu,\n"
        "    \"resize_delay_ms\": %lu\n"
        "  },\n",
        adaptive_buffer_config.min_size,
        adaptive_buffer_config.max_size,
        adaptive_buffer_config.default_size,
        adaptive_buffer_config.threshold_low,
        adaptive_buffer_config.threshold_high,
        adaptive_buffer_config.check_interval_ms,
        adaptive_buffer_config.resize_delay_ms
    );

    offset += snprintf(json_output + offset, buffer_size - offset,
        "  \"udp\": {\n"
        "    \"packet_max_size\": %lu,\n"
        "    \"send_timeout_ms\": %lu,\n"
        "    \"buffer_count\": %u,\n"
        "    \"multicast_group\": \"%s\",\n"
        "    \"multicast_port\": %u\n"
        "  },\n",
        udp_config.packet_max_size,
        udp_config.send_timeout_ms,
        udp_config.buffer_count,
        udp_config.multicast_group,
        udp_config.multicast_port
    );

    offset += snprintf(json_output + offset, buffer_size - offset,
        "  \"performance_monitor\": {\n"
        "    \"interval_ms\": %lu,\n"
        "    \"max_entries\": %zu\n"
        "  }\n",
        performance_monitor_config.interval_ms,
        performance_monitor_config.max_entries
    );

    // Close JSON object
    offset += snprintf(json_output + offset, buffer_size - offset, "}\n");

    if (offset >= buffer_size) {
        ESP_LOGE(TAG, "JSON output buffer too small");
        return false;
    }

    ESP_LOGI(TAG, "Configuration exported to JSON (%d bytes)", offset);
    return true;
}

bool config_manager_import_json(const char* json_input, bool overwrite)
{
    if (!initialized || !json_input) {
        return false;
    }

    ESP_LOGI(TAG, "Importing configuration from JSON (overwrite: %s)", overwrite ? "true" : "false");

    // Note: This is a simplified JSON parser for demonstration
    // In a production environment, you would want to use a proper JSON library
    // For now, we'll use simple string parsing for key values

    // This is a placeholder implementation
    // A full implementation would parse JSON and update the configuration structures
    ESP_LOGW(TAG, "JSON import not fully implemented - placeholder");

    return true;
}

// Audio format helper functions
bool config_manager_validate_audio_format(uint32_t sample_rate, uint8_t bits_per_sample, uint8_t channels)
{
    // Validate sample rate
    if (sample_rate < AUDIO_SAMPLE_RATE_MIN || sample_rate > AUDIO_SAMPLE_RATE_MAX)
    {
        ESP_LOGE(TAG, "Invalid sample rate: %lu (valid range: %d-%d)",
                 sample_rate, AUDIO_SAMPLE_RATE_MIN, AUDIO_SAMPLE_RATE_MAX);
        return false;
    }

    // Validate bit depth
    if (bits_per_sample < AUDIO_BIT_DEPTH_MIN || bits_per_sample > AUDIO_BIT_DEPTH_MAX)
    {
        ESP_LOGE(TAG, "Invalid bit depth: %u (valid range: %d-%d)",
                 bits_per_sample, AUDIO_BIT_DEPTH_MIN, AUDIO_BIT_DEPTH_MAX);
        return false;
    }

    // Validate channels
    if (channels < AUDIO_CHANNELS_MIN || channels > AUDIO_CHANNELS_MAX)
    {
        ESP_LOGE(TAG, "Invalid channel count: %u (valid range: %d-%d)",
                 channels, AUDIO_CHANNELS_MIN, AUDIO_CHANNELS_MAX);
        return false;
    }

    // Check for standard sample rates
    switch (sample_rate)
    {
        case 8000:
        case 11025:
        case 16000:
        case 22050:
        case 32000:
        case 44100:
        case 48000:
        case 96000:
            break; // Valid sample rates
        default:
            ESP_LOGW(TAG, "Non-standard sample rate: %lu", sample_rate);
            break;
    }

    ESP_LOGD(TAG, "Audio format validated: %lu Hz, %u-bit, %u channels",
              sample_rate, bits_per_sample, channels);
    return true;
}

bool config_manager_get_audio_format_name(uint8_t format, char *buffer, size_t buffer_size)
{
    if (!buffer || buffer_size < 16)
        return false;

    switch (format)
    {
        case AUDIO_FORMAT_PCM_8BIT:
            strncpy(buffer, "PCM 8-bit", buffer_size - 1);
            break;
        case AUDIO_FORMAT_PCM_16BIT:
            strncpy(buffer, "PCM 16-bit", buffer_size - 1);
            break;
        case AUDIO_FORMAT_PCM_24BIT:
            strncpy(buffer, "PCM 24-bit", buffer_size - 1);
            break;
        case AUDIO_FORMAT_PCM_32BIT:
            strncpy(buffer, "PCM 32-bit", buffer_size - 1);
            break;
        default:
            snprintf(buffer, buffer_size, "Unknown (%u)", format);
            return false;
    }

    buffer[buffer_size - 1] = '\0';
    return true;
}

bool config_manager_get_sample_rate_name(uint32_t sample_rate, char *buffer, size_t buffer_size)
{
    if (!buffer || buffer_size < 16)
        return false;

    switch (sample_rate)
    {
        case 8000:
            strncpy(buffer, "8 kHz", buffer_size - 1);
            break;
        case 11025:
            strncpy(buffer, "11.025 kHz", buffer_size - 1);
            break;
        case 16000:
            strncpy(buffer, "16 kHz", buffer_size - 1);
            break;
        case 22050:
            strncpy(buffer, "22.05 kHz", buffer_size - 1);
            break;
        case 32000:
            strncpy(buffer, "32 kHz", buffer_size - 1);
            break;
        case 44100:
            strncpy(buffer, "44.1 kHz", buffer_size - 1);
            break;
        case 48000:
            strncpy(buffer, "48 kHz", buffer_size - 1);
            break;
        case 96000:
            strncpy(buffer, "96 kHz", buffer_size - 1);
            break;
        default:
            snprintf(buffer, buffer_size, "%.1f kHz", sample_rate / 1000.0);
            break;
    }

    buffer[buffer_size - 1] = '\0';
    return true;
}

uint32_t config_manager_calculate_audio_data_rate(uint32_t sample_rate, uint8_t bits_per_sample, uint8_t channels)
{
    return sample_rate * bits_per_sample * channels;
}

uint8_t config_manager_format_to_bits_per_sample(uint8_t format)
{
    switch (format)
    {
        case AUDIO_FORMAT_PCM_8BIT:
            return 8;
        case AUDIO_FORMAT_PCM_16BIT:
            return 16;
        case AUDIO_FORMAT_PCM_24BIT:
            return 24;
        case AUDIO_FORMAT_PCM_32BIT:
            return 32;
        default:
            return 16; // Default to 16-bit
    }
}

uint8_t config_manager_bits_per_sample_to_format(uint8_t bits_per_sample)
{
    switch (bits_per_sample)
    {
        case 8:
            return AUDIO_FORMAT_PCM_8BIT;
        case 16:
            return AUDIO_FORMAT_PCM_16BIT;
        case 24:
            return AUDIO_FORMAT_PCM_24BIT;
        case 32:
            return AUDIO_FORMAT_PCM_32BIT;
        default:
            return AUDIO_FORMAT_PCM_16BIT; // Default to 16-bit
    }
}

void config_manager_deinit(void)
{
    if (initialized)
    {
        nvs_close(g_nvs_handle);
        initialized = false;
        ESP_LOGI(TAG, "Config manager deinitialized");
    }
}
