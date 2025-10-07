#ifndef CONFIG_H
#define CONFIG_H

#ifndef WIFI_SSID
#define WIFI_SSID "Sarpel_2G"
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD "penguen1988"
#endif

// Static IP Configuration (optional, set to 0,0,0,0 for DHCP)
#define STATIC_IP_ADDR "0.0.0.0"
#define GATEWAY_ADDR "0.0.0.0"
#define SUBNET_MASK "0.0.0.0"
#define PRIMARY_DNS "0.0.0.0"
#define SECONDARY_DNS "1.1.1.1"  // Added for redundancy

// TCP Server Configuration
#define TCP_SERVER_IP "192.168.1.50"  // LXC container IP
#define TCP_SERVER_PORT 9000

// NTP Configuration
#define NTP_SERVER "pool.ntp.org"
#define NTP_TIMEZONE "UTC+3"  // Adjust for your timezone

// I2S Pin Configuration (Seeed XIAO ESP32-S3)
#define I2S_BCK_PIN 2      // Bit Clock (GPIO 2)
#define I2S_WS_PIN 3       // Word Select / LRCLK (GPIO 3)
#define I2S_DATA_IN_PIN 1  // Serial Data In (GPIO 1)

// Audio Configuration
#define SAMPLE_RATE 16000
#define BITS_PER_SAMPLE 16
#define CHANNELS 1  // Mono
#define BYTES_PER_SAMPLE 2  // 16-bit = 2 bytes per sample

// Buffer Configuration
#define I2S_DMA_BUF_COUNT 8
#define I2S_DMA_BUF_LEN 512  // 512 samples per DMA buffer (1024 for better resilience)
#define RING_BUFFER_SIZE (96 * 1024)  // 128 KB in internal SRAM (ESP32-S3 has 512KB total)

// Task Configuration
#define I2S_READER_STACK_SIZE 4096
#define TCP_SENDER_STACK_SIZE 4096
#define WATCHDOG_STACK_SIZE 4096

// Task priorities (0 = lowest, configMAX_PRIORITIES-1 = highest)
#define I2S_READER_PRIORITY 10  // Highest - real-time audio capture
#define TCP_SENDER_PRIORITY 8   // Medium - network transmission
#define WATCHDOG_PRIORITY 1     // Lowest - background monitoring

// Core assignment (ESP32-S3 has 2 cores: 0 and 1)
// Core 0: WiFi stack runs here, assign TCP sender for network efficiency
// Core 1: Dedicated to I2S for consistent audio capture timing
#define I2S_READER_CORE 1       // Dedicated audio core
#define TCP_SENDER_CORE 1       // Same core as WiFi stack
#define WATCHDOG_CORE 0         // Low priority, share with WiFi/TCP

// Watchdog Configuration
#define WATCHDOG_TIMEOUT_SEC 60
#define NTP_RESYNC_INTERVAL_SEC 3600  // 1 hour

// Debug Configuration
#define DEBUG_ENABLED 1
#define LED_STATUS_PIN -1  // Set to GPIO pin if status LED is connected, -1 to disable

// Error Handling Configuration
#define MAX_RECONNECT_ATTEMPTS 10        // Max TCP reconnect attempts before reboot
#define RECONNECT_BACKOFF_MS 1000        // Start with 1 second
#define MAX_RECONNECT_BACKOFF_MS 30000   // Cap at 30 seconds
#define MAX_I2S_FAILURES 100             // Max consecutive I2S failures before reinit
#define MAX_BUFFER_OVERFLOWS 20          // Max overflows before action
#define OVERFLOW_COOLDOWN_MS 5000        // Wait after overflow detected

// Stack Monitoring
#define ENABLE_STACK_MONITORING 1      // Monitor stack usage
#define MIN_STACK_WATERMARK 512        // Warn if stack < 512 bytes free

// ✅ Add these safety limits
#define MUTEX_TIMEOUT_MS 5000              // Max wait for mutex
#define MAX_WIFI_DISCONNECTS 20            // Reboot after this many

// Recovery Actions
#define ENABLE_AUTO_REBOOT 1             // Reboot on critical failures
#define ENABLE_I2S_REINIT 1              // Reinitialize I2S on persistent failures
#define ENABLE_BUFFER_DRAIN 1            // Force drain buffer on overflow

#endif // CONFIG_H
