#include "tcp_streamer.h"
#include "../config.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include <string.h>

static const char *TAG = "TCP_STREAMER";

static int sock = -1;
static uint64_t total_bytes_sent = 0;
static uint32_t reconnect_count = 0;

// ✅ ADD: Global packing buffer (allocated once at init)
static uint8_t *packing_buffer = NULL;
static size_t packing_buffer_size = 0;

static bool tcp_connect(void)
{
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(TCP_SERVER_PORT);
    inet_pton(AF_INET, TCP_SERVER_IP, &server_addr.sin_addr);

    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0)
    {
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

    // Disable Nagle algorithm for low latency
    int nodelay = 1;
    if (setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay)) != 0)
    {
        ESP_LOGW(TAG, "Failed to set TCP_NODELAY: errno %d", errno);
    }

    // Set send timeout
    struct timeval timeout;
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    // Connect to server
    int ret = connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if (ret != 0)
    {
        ESP_LOGE(TAG, "Failed to connect: errno %d", errno);
        close(sock);
        sock = -1;
        return false;
    }

    return true;
}

bool tcp_streamer_init(void)
{
    // ✅ MOVE: Allocate packing buffer FIRST (before connection attempts)
    packing_buffer_size = 16384 * 2; // Max samples × bytes per sample
    packing_buffer = (uint8_t *)malloc(packing_buffer_size);

    if (packing_buffer == NULL)
    {
        ESP_LOGE(TAG, "Failed to allocate packing buffer");
        return false;
    }

    // Attempt connection with retries
    int max_retries = TCP_CONNECT_MAX_RETRIES;
    int retry_delay_ms = 2000;

    for (int i = 0; i < max_retries; i++)
    {
        if (tcp_connect())
        {
            return true;
        }

        ESP_LOGW(TAG, "Connection attempt %d/%d failed, retrying...", i + 1, max_retries);
        vTaskDelay(pdMS_TO_TICKS(retry_delay_ms));
        retry_delay_ms *= 2; // Exponential backoff
    }

    ESP_LOGE(TAG, "Failed to connect after %d attempts", max_retries);

    // ✅ ADD: Free buffer if connection failed
    if (packing_buffer != NULL)
    {
        free(packing_buffer);
        packing_buffer = NULL;
    }

    return false;
}

bool tcp_streamer_is_connected(void)
{
    if (sock < 0)
    {
        return false;
    }

    // Check socket status
    int error = 0;
    socklen_t len = sizeof(error);
    int ret = getsockopt(sock, SOL_SOCKET, SO_ERROR, &error, &len);

    if (ret != 0 || error != 0)
    {
        return false;
    }

    return true;
}

bool tcp_streamer_send_audio(const int32_t *samples, size_t sample_count)
{
    if (sock < 0 || samples == NULL || sample_count == 0)
    {
        return false;
    }

    size_t packed_size = sample_count * BYTES_PER_SAMPLE;

    // ✅ Check size fits in pre-allocated buffer
    if (packed_size > packing_buffer_size)
    {
        ESP_LOGE(TAG, "Data too large for packing buffer");
        return false;
    }

    // Pack 32-bit samples to 16-bit
    for (size_t i = 0; i < sample_count; i++)
    {
        int16_t sample_16 = (int16_t)(samples[i] >> 16);
        packing_buffer[i * 2] = sample_16 & 0xFF;
        packing_buffer[i * 2 + 1] = (sample_16 >> 8) & 0xFF;
    }

    // Send packed data
    size_t total_sent = 0;
    while (total_sent < packed_size)
    {
        ssize_t sent = send(sock, packing_buffer + total_sent, packed_size - total_sent, 0);

        if (sent < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }
            ESP_LOGE(TAG, "Send failed: errno %d", errno);
            return false;
        }
        else if (sent == 0)
        {
            ESP_LOGW(TAG, "Connection closed");
            return false;
        }

        total_sent += sent;
    }

    total_bytes_sent += packed_size;
    return true;
}

bool tcp_streamer_reconnect(void)
{
    ESP_LOGI(TAG, "Attempting to reconnect...");

    if (sock >= 0)
    {
        close(sock);
        sock = -1;
    }

    reconnect_count++;

    return tcp_connect();
}

void tcp_streamer_close(void)
{
    if (sock >= 0)
    {
        close(sock);
        sock = -1;
        ESP_LOGI(TAG, "Connection closed");
    }
}

// ✅ ADD: New deinit function to cleanup resources
void tcp_streamer_deinit(void)
{
    // Close connection
    tcp_streamer_close();

    // Free packing buffer
    if (packing_buffer != NULL)
    {
        free(packing_buffer);
        packing_buffer = NULL;
        packing_buffer_size = 0;
        ESP_LOGI(TAG, "Packing buffer freed");
    }
}

void tcp_streamer_get_stats(uint64_t *bytes_sent, uint32_t *reconnect_cnt)
{
    if (bytes_sent)
        *bytes_sent = total_bytes_sent;
    if (reconnect_cnt)
        *reconnect_cnt = reconnect_count;
}