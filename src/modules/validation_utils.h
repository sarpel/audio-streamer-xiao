#ifndef VALIDATION_UTILS_H
#define VALIDATION_UTILS_H

#include <stddef.h>
#include <stdbool.h>

/**
 * @brief Validate IPv4 address format
 * 
 * Checks if the provided string is a valid IPv4 address.
 * Accepts dotted decimal notation (e.g., "192.168.1.1").
 * 
 * @param ip IP address string to validate
 * @return true if valid IPv4 address, false otherwise
 */
bool validate_ip_address(const char* ip);

/**
 * @brief Validate port number
 * 
 * Checks if port is within valid range (1-65535).
 * Port 0 is considered invalid.
 * 
 * @param port Port number to validate
 * @return true if valid port, false otherwise
 */
bool validate_port(int port);

/**
 * @brief Validate audio sample rate
 * 
 * Checks if sample rate is one of the supported values.
 * Supported rates: 8000, 16000, 22050, 32000, 44100, 48000 Hz.
 * 
 * @param rate Sample rate in Hz
 * @return true if valid sample rate, false otherwise
 */
bool validate_sample_rate(int rate);

/**
 * @brief Validate buffer size
 * 
 * Checks if buffer size is within reasonable limits.
 * Minimum: 1KB, Maximum: 512KB (ESP32-S3 SRAM limit).
 * 
 * @param size Buffer size in bytes
 * @return true if valid buffer size, false otherwise
 */
bool validate_buffer_size(size_t size);

/**
 * @brief Validate string length
 * 
 * Checks if string doesn't exceed maximum length.
 * Useful for preventing buffer overflows.
 * 
 * @param str String to validate (can be NULL)
 * @param max_len Maximum allowed length (excluding null terminator)
 * @return true if string is valid length, false otherwise
 */
bool validate_string_length(const char* str, size_t max_len);

/**
 * @brief Validate DMA buffer count
 * 
 * Checks if DMA buffer count is within valid range.
 * Minimum: 2, Maximum: 128 buffers.
 * 
 * @param count Number of DMA buffers
 * @return true if valid count, false otherwise
 */
bool validate_dma_buffer_count(int count);

/**
 * @brief Validate DMA buffer length
 * 
 * Checks if DMA buffer length is valid.
 * Must be between 8 and 1024 samples.
 * 
 * @param length Buffer length in samples
 * @return true if valid length, false otherwise
 */
bool validate_dma_buffer_length(int length);

/**
 * @brief Validate task priority
 * 
 * Checks if FreeRTOS task priority is valid.
 * Range: 0-31 (0 = lowest, 31 = highest).
 * 
 * @param priority Task priority value
 * @return true if valid priority, false otherwise
 */
bool validate_task_priority(int priority);

/**
 * @brief Validate CPU core assignment
 * 
 * Checks if core number is valid for ESP32-S3.
 * Valid cores: 0 or 1 (dual-core).
 * 
 * @param core Core number
 * @return true if valid core, false otherwise
 */
bool validate_cpu_core(int core);

#endif // VALIDATION_UTILS_H
