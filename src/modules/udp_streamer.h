#ifndef UDP_STREAMER_H
#define UDP_STREAMER_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/**
 * Initialize UDP streamer
 */
bool udp_streamer_init(void);

/**
 * Check if UDP socket is active
 */
bool udp_streamer_is_connected(void);

/**
 * Send audio samples over UDP (16-bit samples)
 * @param samples Array of 16-bit audio samples
 * @param sample_count Number of samples
 * @return true if sent successfully
 */
bool udp_streamer_send_audio_16(const int16_t *samples, size_t sample_count);

/**
 * Send audio samples over UDP (legacy 32-bit interface)
 * @param samples Array of 32-bit audio samples
 * @param sample_count Number of samples
 * @return true if sent successfully
 */
bool udp_streamer_send_audio(const int32_t *samples, size_t sample_count);

/**
 * Reconnect UDP socket (mainly for configuration changes)
 */
bool udp_streamer_reconnect(void);

/**
 * Close UDP socket
 */
void udp_streamer_close(void);

/**
 * Cleanup and deinitialize UDP streamer
 */
void udp_streamer_deinit(void);

/**
 * Get streaming statistics
 */
void udp_streamer_get_stats(uint64_t *bytes_sent, uint32_t *packets_sent, uint32_t *lost_packets);

#endif // UDP_STREAMER_H