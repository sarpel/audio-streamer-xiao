#ifndef TCP_STREAMER_H
#define TCP_STREAMER_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/**
 * Initialize TCP streamer and connect to server
 */
bool tcp_streamer_init(void);

/**
 * Check if TCP connection is active
 */
bool tcp_streamer_is_connected(void);

/**
 * Send audio samples over TCP (16-bit samples)
 * @param samples Array of 16-bit audio samples
 * @param sample_count Number of samples
 * @return true if sent successfully
 */
bool tcp_streamer_send_audio_16(const int16_t *samples, size_t sample_count);

/**
 * Send audio samples over TCP (legacy 32-bit interface)
 * @param samples Array of 32-bit audio samples
 * @param sample_count Number of samples
 * @return true if sent successfully
 */
bool tcp_streamer_send_audio(const int32_t *samples, size_t sample_count);

/**
 * Reconnect to TCP server
 */
bool tcp_streamer_reconnect(void);

/**
 * Close TCP connection
 */
void tcp_streamer_close(void);

/**
 * Cleanup and deinitialize TCP streamer
 */
void tcp_streamer_deinit(void); // ✅ ADD THIS

/**
 * Get streaming statistics
 */
void tcp_streamer_get_stats(uint64_t *bytes_sent, uint32_t *reconnect_cnt);

#endif // TCP_STREAMER_H
