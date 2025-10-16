#include "udp_streamer.h"
#include "../config.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "UDP_STREAMER";

static int sock = -1;
static struct sockaddr_in server_addr;
static uint64_t total_bytes_sent = 0;
static uint32_t total_packets_sent = 0;
static uint32_t lost_packets = 0;
static uint32_t packet_sequence = 0;

// UDP configuration buffer for packet headers
static uint8_t *udp_buffer = NULL;
static size_t udp_buffer_size = 0;

// UDP packet header structure
typedef struct {
    uint32_t sequence;     // Packet sequence number
    uint32_t timestamp;    // Timestamp in milliseconds
    uint16_t sample_count; // Number of samples in this packet
    uint16_t flags;        // Flags (bit 0: start of stream, bit 1: end of stream)
} __attribute__((packed)) udp_packet_header_t;

static bool udp_connect(void)
{
    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(UDP_SERVER_PORT);
    inet_pton(AF_INET, UDP_SERVER_IP, &server_addr.sin_addr);

    ESP_LOGI(TAG, "Setting up UDP for %s:%d...", UDP_SERVER_IP, UDP_SERVER_PORT);

    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0)
    {
        ESP_LOGE(TAG, "Failed to create UDP socket: errno %d", errno);
        return false;
    }

    // Set socket options for UDP with optimized values
    int reuse = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

#if NETWORK_OPTIMIZATION_ENABLED
    // Set buffer sizes for better performance
    int send_buf_size = UDP_TX_BUFFER_SIZE;
    int recv_buf_size = UDP_RX_BUFFER_SIZE;

    if (setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &send_buf_size, sizeof(send_buf_size)) != 0)
    {
        ESP_LOGW(TAG, "Failed to set UDP TX buffer size: errno %d", errno);
    }
    else
    {
        ESP_LOGI(TAG, "UDP TX buffer set to %d bytes", send_buf_size);
    }

    if (setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &recv_buf_size, sizeof(recv_buf_size)) != 0)
    {
        ESP_LOGW(TAG, "Failed to set UDP RX buffer size: errno %d", errno);
    }
    else
    {
        ESP_LOGI(TAG, "UDP RX buffer set to %d bytes", recv_buf_size);
    }
#endif

    // Set timeout for send operations
    struct timeval timeout;
    timeout.tv_sec = 1;  // 1 second timeout
    timeout.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    ESP_LOGI(TAG, "UDP socket initialized successfully");
    return true;
}

bool udp_streamer_init(void)
{
    // Allocate UDP buffer (header + audio data)
    // Calculate max audio payload based on configuration constants instead of magic number
    size_t max_audio_payload = TCP_SEND_SAMPLES * BYTES_PER_SAMPLE; // Max samples × bytes per sample
    // Add safety margin for packet headers and potential overhead
    max_audio_payload += 1024;
    udp_buffer_size = sizeof(udp_packet_header_t) + max_audio_payload;
    udp_buffer = (uint8_t *)malloc(udp_buffer_size);

    if (udp_buffer == NULL)
    {
        ESP_LOGE(TAG, "Failed to allocate UDP buffer");
        return false;
    }

    ESP_LOGI(TAG, "UDP buffer allocated: %zu bytes", udp_buffer_size);

    // Initialize UDP socket
    if (!udp_connect())
    {
        free(udp_buffer);
        udp_buffer = NULL;
        return false;
    }

    // Reset statistics
    total_bytes_sent = 0;
    total_packets_sent = 0;
    lost_packets = 0;
    packet_sequence = 0;

    ESP_LOGI(TAG, "UDP streamer initialized");
    return true;
}

bool udp_streamer_is_connected(void)
{
    return sock >= 0;
}

bool udp_streamer_send_audio_16(const int16_t *samples, size_t sample_count)
{
    if (sock < 0 || samples == NULL || sample_count == 0)
    {
        return false;
    }

    size_t data_size = sample_count * sizeof(int16_t);
    size_t packet_size = sizeof(udp_packet_header_t) + data_size;

    if (packet_size > udp_buffer_size)
    {
        ESP_LOGE(TAG, "Data too large for UDP buffer");
        return false;
    }

    // Prepare packet header
    udp_packet_header_t *header = (udp_packet_header_t *)udp_buffer;
    header->sequence = packet_sequence++;
    header->timestamp = xTaskGetTickCount() * portTICK_PERIOD_MS;
    header->sample_count = sample_count;
    header->flags = 0; // Normal packet

    // Copy audio data after header
    memcpy(udp_buffer + sizeof(udp_packet_header_t), samples, data_size);

    // Send UDP packet
    ssize_t sent = sendto(sock, udp_buffer, packet_size, 0,
                         (struct sockaddr *)&server_addr, sizeof(server_addr));

    if (sent < 0)
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            ESP_LOGW(TAG, "UDP send timeout, packet lost");
            lost_packets++;
            return false;
        }
        else
        {
            ESP_LOGE(TAG, "UDP send failed: errno %d", errno);
            lost_packets++;
            return false;
        }
    }
    else if (sent != packet_size)
    {
        ESP_LOGW(TAG, "Partial UDP send: %zd/%zu bytes", sent, packet_size);
        lost_packets++;
        return false;
    }

    total_bytes_sent += data_size;
    total_packets_sent++;
    return true;
}

bool udp_streamer_send_audio(const int32_t *samples, size_t sample_count)
{
    if (sock < 0 || samples == NULL || sample_count == 0)
    {
        return false;
    }

    size_t data_size = sample_count * sizeof(int16_t); // After conversion to 16-bit
    size_t packet_size = sizeof(udp_packet_header_t) + data_size;

    if (packet_size > udp_buffer_size)
    {
        ESP_LOGE(TAG, "Data too large for UDP buffer");
        return false;
    }

    // Prepare packet header
    udp_packet_header_t *header = (udp_packet_header_t *)udp_buffer;
    header->sequence = packet_sequence++;
    header->timestamp = xTaskGetTickCount() * portTICK_PERIOD_MS;
    header->sample_count = sample_count;
    header->flags = 0; // Normal packet

    // Convert 32-bit samples to 16-bit and copy after header
    int16_t *audio_data = (int16_t *)(udp_buffer + sizeof(udp_packet_header_t));
    for (size_t i = 0; i < sample_count; i++)
    {
        audio_data[i] = (int16_t)(samples[i] >> 16);
    }

    // Send UDP packet
    ssize_t sent = sendto(sock, udp_buffer, packet_size, 0,
                         (struct sockaddr *)&server_addr, sizeof(server_addr));

    if (sent < 0)
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            ESP_LOGW(TAG, "UDP send timeout, packet lost");
            lost_packets++;
            return false;
        }
        else
        {
            ESP_LOGE(TAG, "UDP send failed: errno %d", errno);
            lost_packets++;
            return false;
        }
    }
    else if (sent != packet_size)
    {
        ESP_LOGW(TAG, "Partial UDP send: %zd/%zu bytes", sent, packet_size);
        lost_packets++;
        return false;
    }

    total_bytes_sent += data_size;
    total_packets_sent++;
    return true;
}

bool udp_streamer_reconnect(void)
{
    ESP_LOGI(TAG, "Reconnecting UDP socket...");

    if (sock >= 0)
    {
        close(sock);
        sock = -1;
    }

    return udp_connect();
}

void udp_streamer_close(void)
{
    if (sock >= 0)
    {
        close(sock);
        sock = -1;
        ESP_LOGI(TAG, "UDP socket closed");
    }
}

void udp_streamer_deinit(void)
{
    // Close socket
    udp_streamer_close();

    // Free UDP buffer
    if (udp_buffer != NULL)
    {
        free(udp_buffer);
        udp_buffer = NULL;
        udp_buffer_size = 0;
        ESP_LOGI(TAG, "UDP buffer freed");
    }
}

void udp_streamer_get_stats(uint64_t *bytes_sent, uint32_t *packets_sent, uint32_t *lost_pkt)
{
    if (bytes_sent)
        *bytes_sent = total_bytes_sent;
    if (packets_sent)
        *packets_sent = total_packets_sent;
    if (lost_pkt)
        *lost_pkt = lost_packets;
}