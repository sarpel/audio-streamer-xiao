#ifndef CONFIG_H
#define CONFIG_H

#ifndef WIFI_SSID
#define WIFI_SSID "Sarpel_2G"
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD "penguen1988"
#endif

// Web Authentication Configuration
#define WEB_AUTH_USERNAME "sarpel"
#define WEB_AUTH_PASSWORD "13524678"

// Captive Portal Configuration
#define CAPTIVE_PORTAL_SSID "AudioStreamer-Setup"
#define CAPTIVE_PORTAL_TIMEOUT_SEC 300 // 5 minutes before switching to normal mode (3-strike system)

// Static IP Configuration (optional, set to 0,0,0,0 for DHCP)
#define STATIC_IP_ADDR "0.0.0.0"
#define GATEWAY_ADDR "0.0.0.0"
#define SUBNET_MASK "0.0.0.0"
#define PRIMARY_DNS "0.0.0.0"
#define SECONDARY_DNS "0.0.0.0" // Added for redundancy

// TCP Server Configuration
#define TCP_SERVER_IP "192.168.1.50" // LXC container IP
#define TCP_SERVER_PORT 9000

// UDP Server Configuration
#define UDP_SERVER_IP "192.168.1.50" // Default to same as TCP
#define UDP_SERVER_PORT 9001         // Different port to avoid conflicts

// NTP Configuration
#define NTP_SERVER "pool.ntp.org"
#define NTP_TIMEZONE "UTC-3" // Adjust for your timezone

// I2S pins (XIAO ESP32S3)
#define I2S_BCLK_GPIO 2 // BCLK
#define I2S_WS_GPIO 3   // LRCLK/WS
#define I2S_SD_GPIO 1   // DATA IN (SD)

// Audio format options
typedef enum
{
    AUDIO_FORMAT_PCM_8BIT = 0,
    AUDIO_FORMAT_PCM_16BIT = 1,
    AUDIO_FORMAT_PCM_24BIT = 2,
    AUDIO_FORMAT_PCM_32BIT = 3
} audio_format_t;

typedef enum
{
    AUDIO_SAMPLE_RATE_8K = 8000,
    AUDIO_SAMPLE_RATE_11K = 11025,
    AUDIO_SAMPLE_RATE_16K = 16000,
    AUDIO_SAMPLE_RATE_22K = 22050,
    AUDIO_SAMPLE_RATE_32K = 32000,
    AUDIO_SAMPLE_RATE_44K = 44100,
    AUDIO_SAMPLE_RATE_48K = 48000,
    AUDIO_SAMPLE_RATE_96K = 96000
} audio_sample_rate_t;

typedef enum
{
    AUDIO_CHANNELS_MONO = 1,
    AUDIO_CHANNELS_STEREO = 2
} audio_channels_t;

// Audio format configuration
#define AUDIO_FORMAT_DEFAULT AUDIO_FORMAT_PCM_16BIT
#define AUDIO_SAMPLE_RATE_DEFAULT AUDIO_SAMPLE_RATE_16K
#define AUDIO_CHANNELS_DEFAULT AUDIO_CHANNELS_MONO

// I2S configuration
#define I2S_SLOT_BIT_WIDTH 32 // 32-bit slot (INMP441 produces 24-bit data)
#define I2S_READ_SAMPLES 256
#define TCP_SEND_SAMPLES 4096

// Audio format validation ranges
#define AUDIO_SAMPLE_RATE_MIN 8000
#define AUDIO_SAMPLE_RATE_MAX 96000
#define AUDIO_CHANNELS_MIN 1
#define AUDIO_CHANNELS_MAX 2
#define AUDIO_BIT_DEPTH_MIN 8
#define AUDIO_BIT_DEPTH_MAX 32

// Legacy compatibility definitions (for backward compatibility)
#define SAMPLE_RATE 16000
#define BITS_PER_SAMPLE 16
#define CHANNELS 1         // Mono
#define BYTES_PER_SAMPLE 2 // 16-bit = 2 bytes per sample

// Buffer Configuration
#define I2S_DMA_BUF_COUNT 8
#define I2S_DMA_BUF_LEN 512          // 512 samples per DMA buffer (1024 for better resilience)
#define RING_BUFFER_SIZE (48 * 1024) // 48 KB - reduced to fit fragmented memory after WiFi init

// Task Configuration
#define I2S_READER_STACK_SIZE 4096
#define TCP_SENDER_STACK_SIZE 20480
#define WATCHDOG_STACK_SIZE 4096

// Task priorities (0 = lowest, configMAX_PRIORITIES-1 = highest)
#define I2S_READER_PRIORITY 10 // Highest - real-time audio capture
#define TCP_SENDER_PRIORITY 8  // Medium - network transmission
#define WATCHDOG_PRIORITY 1    // Lowest - background monitoring

// Core assignment (ESP32-S3 has 2 cores: 0 and 1)
// Core 0: WiFi stack runs here, assign TCP sender for network efficiency
// Core 1: Dedicated to I2S for consistent audio capture timing
#define I2S_READER_CORE 1 // Dedicated audio core
#define TCP_SENDER_CORE 0 // Same core as WiFi stack
#define WATCHDOG_CORE 0   // Low priority, share with WiFi/TCP

// Watchdog Configuration
#define WATCHDOG_TIMEOUT_SEC 60
#define WATCHDOG_LOG_INTERVAL_SEC 10
#define NTP_RESYNC_INTERVAL_SEC 3600 // 1 hour

// Debug Configuration
#define DEBUG_ENABLED 1
#define LED_STATUS_PIN -1 // Set to GPIO pin if status LED is connected, -1 to disable

// Error Handling Configuration
#define MAX_RECONNECT_ATTEMPTS 10      // Max TCP reconnect attempts before reboot
#define RECONNECT_BACKOFF_MS 1000      // Start with 1 second
#define MAX_RECONNECT_BACKOFF_MS 30000 // Cap at 30 seconds
#define MAX_I2S_FAILURES 100           // Max consecutive I2S failures before reinit
#define MAX_BUFFER_OVERFLOWS 20        // Max overflows before action
#define OVERFLOW_COOLDOWN_MS 5000      // Wait after overflow detected

// Thresholds for error detection (moved from hardcoded values)
#define I2S_UNDERFLOW_THRESHOLD 100 // Max I2S underflows before action
#define WIFI_CONNECT_MAX_RETRIES 20 // Max WiFi connection attempts
#define TCP_CONNECT_MAX_RETRIES 5   // Max TCP connection attempts per cycle

// Stack Monitoring
#define ENABLE_STACK_MONITORING 1 // Monitor stack usage
#define MIN_STACK_WATERMARK 512   // Warn if stack < 512 bytes free

// ✅ Add these safety limits
#define MUTEX_TIMEOUT_MS 5000   // Max wait for mutex
#define MAX_WIFI_DISCONNECTS 20 // Reboot after this many

// Recovery Actions
#define ENABLE_AUTO_REBOOT 1  // Reboot on critical failures
#define ENABLE_I2S_REINIT 1   // Reinitialize I2S on persistent failures
#define ENABLE_BUFFER_DRAIN 1 // Force drain buffer on overflow

// Streaming Protocol Configuration
#define STREAMING_PROTOCOL_TCP 0
#define STREAMING_PROTOCOL_UDP 1
#define STREAMING_PROTOCOL_BOTH 2

#ifndef STREAMING_PROTOCOL
#define STREAMING_PROTOCOL STREAMING_PROTOCOL_TCP
#endif

// UDP Specific Configuration
#define UDP_PACKET_MAX_SIZE 1472 // Safe UDP packet size (under Ethernet MTU)
#define UDP_SEND_TIMEOUT_MS 100  // Timeout for UDP sends
#define UDP_BUFFER_COUNT 8       // Number of UDP packets to buffer

// Adaptive Buffering Configuration
#define ADAPTIVE_BUFFERING_ENABLED 1
#define ADAPTIVE_BUFFER_MIN_SIZE (32 * 1024)     // 32KB minimum
#define ADAPTIVE_BUFFER_MAX_SIZE (256 * 1024)    // 256KB maximum
#define ADAPTIVE_BUFFER_DEFAULT_SIZE (96 * 1024) // 96KB default
#define ADAPTIVE_THRESHOLD_LOW 30                // Resize down if usage < 30%
#define ADAPTIVE_THRESHOLD_HIGH 80               // Resize up if usage > 80%
#define ADAPTIVE_CHECK_INTERVAL_MS 5000          // Check every 5 seconds
#define ADAPTIVE_RESIZE_DELAY_MS 10000           // Wait 10s between resizes

// Network Stack Optimization Configuration
#define NETWORK_OPTIMIZATION_ENABLED 1

// TCP Optimization
#define TCP_KEEPALIVE_ENABLED 1
#define TCP_KEEPALIVE_IDLE_SEC 30    // Start keepalive after 30s
#define TCP_KEEPALIVE_INTERVAL_SEC 5 // Send keepalive every 5s
#define TCP_KEEPALIVE_COUNT 3        // 3 keepalive attempts before giving up
#define TCP_NODELAY_ENABLED 1        // Disable Nagle's algorithm for low latency
#define TCP_TX_BUFFER_SIZE 32768     // 32KB TCP send buffer
#define TCP_RX_BUFFER_SIZE 32768     // 32KB TCP receive buffer

// UDP Optimization
#define UDP_TX_BUFFER_SIZE 65536 // 64KB UDP send buffer
#define UDP_RX_BUFFER_SIZE 65536 // 64KB UDP receive buffer

// WiFi Optimization
#define WIFI_POWER_SAVE_DISABLED 1 // Disable power save for better performance
#define WIFI_STATIC_NVS_ENABLED 1  // Enable static NVS allocation
#define WIFI_DYNAMIC_TX_BUF_NUM 16 // Number of dynamic TX buffers
#define WIFI_STATIC_TX_BUF_NUM 8   // Number of static TX buffers
#define WIFI_DYNAMIC_RX_BUF_NUM 16 // Number of dynamic RX buffers
#define WIFI_STATIC_RX_BUF_NUM 8   // Number of static RX buffers
#define WIFI_RX_MGMT_BUF_NUM 10    // Number of management buffers
#define WIFI_RX_BA_WIN 8           // Block ACK window size

// Network Performance Tuning
#define NETWORK_TASK_STACK_SIZE 4096        // Network task stack size
#define NETWORK_EVENT_QUEUE_SIZE 16         // Network event queue size
#define NETWORK_EVENT_QUEUE_TIMEOUT_MS 1000 // Event queue timeout

// IP Configuration
#define IP_FRAGMENT_TTL 64 // IP fragment TTL
#define ARP_TABLE_SIZE 10  // ARP table size

// Performance Monitoring Configuration
#define MAX_ALERTS 100 // Maximum alerts to store
// Note: HISTORY_INTERVAL_MS and MAX_HISTORY_ENTRIES are defined in performance_monitor.h

#endif // CONFIG_H
