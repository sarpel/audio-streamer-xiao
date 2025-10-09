#include "validation_utils.h"
#include <string.h>
#include <ctype.h>
#include <arpa/inet.h>

// Supported sample rates (in Hz)
static const int VALID_SAMPLE_RATES[] = {8000, 16000, 22050, 32000, 44100, 48000};
static const int NUM_VALID_SAMPLE_RATES = sizeof(VALID_SAMPLE_RATES) / sizeof(VALID_SAMPLE_RATES[0]);

// Buffer size limits (in bytes)
static const size_t MIN_BUFFER_SIZE = 1024;        // 1 KB
static const size_t MAX_BUFFER_SIZE = 512 * 1024;  // 512 KB

// DMA buffer limits
static const int MIN_DMA_BUF_COUNT = 2;
static const int MAX_DMA_BUF_COUNT = 128;
static const int MIN_DMA_BUF_LENGTH = 8;
static const int MAX_DMA_BUF_LENGTH = 1024;

// Task configuration limits
static const int MIN_TASK_PRIORITY = 0;
static const int MAX_TASK_PRIORITY = 31;  // FreeRTOS max priority
static const int MIN_CPU_CORE = 0;
static const int MAX_CPU_CORE = 1;        // ESP32-S3 has 2 cores (0, 1)

bool validate_ip_address(const char* ip) {
    if (!ip || strlen(ip) == 0) {
        return false;
    }

    // Try to convert IP string to binary form
    struct in_addr addr;
    int result = inet_pton(AF_INET, ip, &addr);
    
    // inet_pton returns 1 for valid IPv4, 0 for invalid, -1 for error
    return (result == 1);
}

bool validate_port(int port) {
    // Port must be in valid range (1-65535)
    // Port 0 is reserved and not used
    return (port > 0 && port <= 65535);
}

bool validate_sample_rate(int rate) {
    // Check if rate is in the list of valid sample rates
    for (int i = 0; i < NUM_VALID_SAMPLE_RATES; i++) {
        if (rate == VALID_SAMPLE_RATES[i]) {
            return true;
        }
    }
    return false;
}

bool validate_buffer_size(size_t size) {
    // Buffer size must be within reasonable limits
    return (size >= MIN_BUFFER_SIZE && size <= MAX_BUFFER_SIZE);
}

bool validate_string_length(const char* str, size_t max_len) {
    if (!str) {
        return false;
    }
    
    size_t len = strlen(str);
    return (len > 0 && len <= max_len);
}

bool validate_dma_buffer_count(int count) {
    // DMA buffer count must be reasonable
    return (count >= MIN_DMA_BUF_COUNT && count <= MAX_DMA_BUF_COUNT);
}

bool validate_dma_buffer_length(int length) {
    // DMA buffer length must be valid
    return (length >= MIN_DMA_BUF_LENGTH && length <= MAX_DMA_BUF_LENGTH);
}

bool validate_task_priority(int priority) {
    // FreeRTOS task priority must be in valid range
    return (priority >= MIN_TASK_PRIORITY && priority <= MAX_TASK_PRIORITY);
}

bool validate_cpu_core(int core) {
    // ESP32-S3 has cores 0 and 1
    return (core >= MIN_CPU_CORE && core <= MAX_CPU_CORE);
}
