#ifndef TCP_STREAMER_H
#define TCP_STREAMER_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/**
 * Initialize TCP client and connect to server
 *
 * Creates persistent TCP connection to LXC container.
 * Retries on connection failure.
 *
 * @return true on successful connection, false on failure
 */
bool tcp_streamer_init(void);

/**
 * Check if TCP connection is active
 *
 * @return true if connected, false otherwise
 */
bool tcp_streamer_is_connected(void);

/**
 * Send raw PCM data over TCP
 *
 * Converts int32_t samples to 24-bit packed format (3 bytes/sample).
 * Handles backpressure by blocking until data is sent.
 *
 * @param samples Pointer to int32_t samples
 * @param sample_count Number of samples to send
 * @return true on success, false on failure
 */
bool tcp_streamer_send_audio(const int32_t* samples, size_t sample_count);

/**
 * Reconnect to TCP server
 *
 * Closes existing connection and attempts new connection.
 *
 * @return true on successful reconnection, false on failure
 */
bool tcp_streamer_reconnect(void);

/**
 * Close TCP connection
 */
void tcp_streamer_close(void);

/**
 * Get TCP statistics
 *
 * @param bytes_sent Pointer to store total bytes sent
 * @param reconnect_count Pointer to store reconnection count
 */
void tcp_streamer_get_stats(uint64_t* bytes_sent, uint32_t* reconnect_count);

#endif // TCP_STREAMER_H
