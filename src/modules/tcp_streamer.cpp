#include "tcp_streamer.h"
#include "../config.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include <string.h>

static const char* TAG = "TCP_STREAMER";

static int sock = -1;
static uint64_t total_bytes_sent = 0;
static uint32_t reconnect_count = 0;

static bool tcp_connect(void) {
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(TCP_SERVER_PORT);
    inet_pton(AF_INET, TCP_SERVER_IP, &server_addr.sin_addr);

    ESP_LOGI(TAG, "Connecting to %s:%d...", TCP_SERVER_IP, TCP_SERVER_PORT);

    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Failed to create socket: errno %d", errno);
        return false;
    }

    // Set socket options
    int keepalive = 1;
    int keepidle = 5;
    int keepinterval = 5;
    int keepcount = 3;

    setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive));
    setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, &keepidle, sizeof(keepidle));
    setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, &keepinterval, sizeof(keepinterval));
    setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, &keepcount, sizeof(keepcount));

    // Set send timeout
    struct timeval timeout;
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    // Connect to server
    int ret = connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr));
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to connect: errno %d", errno);
        close(sock);
        sock = -1;
        return false;
    }

    ESP_LOGI(TAG, "Connected successfully");
    return true;
}

bool tcp_streamer_init(void) {
    // Attempt connection with retries
    int max_retries = 5;
    int retry_delay_ms = 2000;

    for (int i = 0; i < max_retries; i++) {
        if (tcp_connect()) {
            return true;
        }

        ESP_LOGW(TAG, "Connection attempt %d/%d failed, retrying...", i + 1, max_retries);
        vTaskDelay(pdMS_TO_TICKS(retry_delay_ms));
        retry_delay_ms *= 2;  // Exponential backoff
    }

    ESP_LOGE(TAG, "Failed to connect after %d attempts", max_retries);
    return false;
}

bool tcp_streamer_is_connected(void) {
    if (sock < 0) {
        return false;
    }

    // Check socket status
    int error = 0;
    socklen_t len = sizeof(error);
    int ret = getsockopt(sock, SOL_SOCKET, SO_ERROR, &error, &len);

    if (ret != 0 || error != 0) {
        return false;
    }

    return true;
}

bool tcp_streamer_send_audio(const int32_t* samples, size_t sample_count) {
    if (sock < 0 || samples == NULL || sample_count == 0) {
        return false;
    }

    // Pack samples according to configured bit depth
    size_t packed_size = sample_count * BYTES_PER_SAMPLE;
    uint8_t* packed_data = (uint8_t*)malloc(packed_size);
    if (packed_data == NULL) {
        ESP_LOGE(TAG, "Failed to allocate packing buffer");
        return false;
    }

    // Convert int32_t samples to packed format based on bit depth
    if (BITS_PER_SAMPLE == 16) {
        // 16-bit: Take lower 16 bits from int32_t samples
        for (size_t i = 0; i < sample_count; i++) {
            int16_t sample16 = (int16_t)(samples[i] & 0xFFFF);

            #if DEBUG_ENABLED
            // Validate sample range in debug mode
            if (samples[i] < -32768 || samples[i] > 32767) {
                ESP_LOGW(TAG, "Sample %d out of 16-bit range: %d", i, samples[i]);
            }
            #endif

            packed_data[i * 2 + 0] = sample16 & 0xFF;        // LSB
            packed_data[i * 2 + 1] = (sample16 >> 8) & 0xFF; // MSB
        }
    } else if (BITS_PER_SAMPLE == 24) {
        // 24-bit: Take most significant 24 bits from int32_t samples
        for (size_t i = 0; i < sample_count; i++) {
            int32_t sample = samples[i];

            #if DEBUG_ENABLED
            // Validate sample range in debug mode
            if (sample < -8388608 || sample > 8388607) {
                ESP_LOGW(TAG, "Sample %d out of 24-bit range: %d", i, sample);
            }
            #endif

            packed_data[i * 3 + 0] = (sample >> 8) & 0xFF;   // LSB
            packed_data[i * 3 + 1] = (sample >> 16) & 0xFF;  // Middle byte
            packed_data[i * 3 + 2] = (sample >> 24) & 0xFF;  // MSB
        }
    } else {
        // Unsupported bit depth
        ESP_LOGE(TAG, "Unsupported bit depth: %d", BITS_PER_SAMPLE);
        free(packed_data);
        return false;
    }

    // Send packed data
    size_t total_sent = 0;
    while (total_sent < packed_size) {
        ssize_t sent = send(sock, packed_data + total_sent, packed_size - total_sent, 0);
        if (sent < 0) {
            ESP_LOGE(TAG, "Send failed: errno %d", errno);
            free(packed_data);
            return false;
        }
        total_sent += sent;
    }

    total_bytes_sent += packed_size;
    free(packed_data);

    return true;
}

bool tcp_streamer_reconnect(void) {
    ESP_LOGI(TAG, "Attempting to reconnect...");

    if (sock >= 0) {
        close(sock);
        sock = -1;
    }

    reconnect_count++;

    return tcp_connect();
}

void tcp_streamer_close(void) {
    if (sock >= 0) {
        close(sock);
        sock = -1;
        ESP_LOGI(TAG, "Connection closed");
    }
}

void tcp_streamer_get_stats(uint64_t* bytes_sent, uint32_t* reconnect_cnt) {
    if (bytes_sent) *bytes_sent = total_bytes_sent;
    if (reconnect_cnt) *reconnect_cnt = reconnect_count;
}
