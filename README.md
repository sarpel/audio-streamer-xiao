# ESP32-S3 Audio Streamer Firmware

PlatformIO firmware for streaming I2S audio from INMP441 microphone over WiFi/TCP.

## Features

- **I2S Audio Capture**: 24-bit @ 48 kHz from INMP441
- **WiFi Streaming**: Persistent TCP connection to server
- **Large Ring Buffer**: 512 KB in PSRAM for reliability
- **Watchdog Monitoring**: Auto-restart on task failures
- **NTP Time Sync**: Accurate timestamps
- **Dual-Core Architecture**: Optimized task distribution

## Quick Start

1. **Install PlatformIO**:
   ```bash
   pip install platformio
   ```

2. **Configure WiFi** in `src/config.h`:
   ```cpp
   #define WIFI_SSID "your_wifi"
   #define WIFI_PASSWORD "your_password"
   #define TCP_SERVER_IP "192.168.1.50"
   ```

3. **Build and Upload**:
   ```bash
   pio run --target upload --environment xiao_esp32s3
   ```

4. **Monitor**:
   ```bash
   pio device monitor
   ```

## Project Structure

```
audio-streamer-xiao/
├── platformio.ini          # PlatformIO configuration
├── src/
│   ├── main.cpp            # Main application and task orchestration
│   ├── config.h            # Configuration (WiFi, pins, buffer sizes)
│   └── modules/
│       ├── i2s_handler.cpp/h       # I2S driver and DMA management
│       ├── network_manager.cpp/h   # WiFi and NTP
│       ├── tcp_streamer.cpp/h      # TCP client and streaming
│       └── buffer_manager.cpp/h    # Ring buffer in PSRAM
└── README.md
```

## Configuration

### WiFi Settings

Edit `src/config.h`:

```cpp
// WiFi credentials
#define WIFI_SSID "YourNetworkName"
#define WIFI_PASSWORD "YourPassword"

// Static IP (optional, use "0.0.0.0" for DHCP)
#define STATIC_IP_ADDR "192.168.1.100"
#define GATEWAY_ADDR "192.168.1.1"
#define SUBNET_MASK "255.255.255.0"
```

### Server Settings

```cpp
#define TCP_SERVER_IP "192.168.1.50"  // LXC container IP
#define TCP_SERVER_PORT 9000           // TCP port
```

### I2S Pin Configuration

Default for XIAO ESP32-S3:

```cpp
#define I2S_BCK_PIN 2      // Bit Clock
#define I2S_WS_PIN 3       // Word Select
#define I2S_DATA_IN_PIN 1  // Serial Data In
```

### Buffer Configuration

```cpp
#define RING_BUFFER_SIZE (512 * 1024)  // 512 KB in PSRAM
#define I2S_DMA_BUF_COUNT 8            // Number of DMA buffers
#define I2S_DMA_BUF_LEN 512            // Samples per DMA buffer
```

## Architecture

### Task Design

Three FreeRTOS tasks running on dual-core ESP32-S3:

```
Core 0:                    Core 1:
┌────────────────┐        ┌────────────────┐
│ Watchdog Task  │        │ I2S Reader     │
│ (Priority 1)   │        │ (Priority 5)   │
│                │        │                │
│ • WiFi monitor │        │ • Read from    │
│ • NTP resync   │        │   I2S DMA      │
│ • Task health  │        │ • Write to     │
│                │        │   ring buffer  │
└────────────────┘        └────────────────┘
                          ┌────────────────┐
                          │ TCP Sender     │
                          │ (Priority 3)   │
                          │                │
                          │ • Read from    │
                          │   ring buffer  │
                          │ • Pack 24-bit  │
                          │ • Send TCP     │
                          └────────────────┘
```

### Data Flow

```
INMP441 Mic
    ↓ (I2S @ 24-bit/48kHz)
I2S DMA Buffer (8 × 512 samples)
    ↓ (i2s_read)
Ring Buffer (512 KB in PSRAM)
    ↓ (buffer_manager_read)
24-bit Packing (3 bytes/sample)
    ↓ (tcp_streamer_send_audio)
TCP Socket → Server
```

## Module Documentation

### I2S Handler (`i2s_handler.cpp`)

Manages I2S peripheral for audio capture from INMP441.

**Key Functions:**
- `i2s_handler_init()`: Initialize I2S driver with DMA
- `i2s_handler_read()`: Read samples from DMA buffer
- `i2s_handler_get_stats()`: Get overflow/underflow counts

**Configuration:**
- 48 kHz sample rate
- 24-bit samples (in 32-bit containers)
- Mono (left channel only)
- 8 DMA buffers × 512 samples

### Network Manager (`network_manager.cpp`)

Handles WiFi connection and NTP time synchronization.

**Key Functions:**
- `network_manager_init()`: Connect to WiFi
- `network_manager_is_connected()`: Check connection status
- `network_manager_init_ntp()`: Sync time with NTP server
- `network_manager_resync_ntp()`: Periodic NTP resync

**Features:**
- Static IP or DHCP
- Auto-reconnect on disconnect
- Power save disabled for streaming
- NTP sync every hour

### TCP Streamer (`tcp_streamer.cpp`)

Manages TCP connection and audio data transmission.

**Key Functions:**
- `tcp_streamer_init()`: Connect to TCP server
- `tcp_streamer_send_audio()`: Send 24-bit packed audio
- `tcp_streamer_reconnect()`: Reconnect on failure
- `tcp_streamer_get_stats()`: Get bytes sent and reconnect count

**Features:**
- Persistent TCP connection
- Automatic reconnection with exponential backoff
- 24-bit packing (3 bytes/sample from 32-bit)
- Socket keepalive

### Buffer Manager (`buffer_manager.cpp`)

Thread-safe ring buffer for audio samples.

**Key Functions:**
- `buffer_manager_init()`: Allocate buffer in PSRAM
- `buffer_manager_write()`: Write samples (from I2S task)
- `buffer_manager_read()`: Read samples (from TCP task)
- `buffer_manager_usage_percent()`: Get buffer utilization

**Features:**
- 512 KB capacity in PSRAM
- Mutex-protected operations
- Overflow detection
- Usage monitoring

## Serial Monitor Output

### Normal Operation

```
I (1234) MAIN: === Audio Streamer Starting ===
I (1250) NETWORK_MANAGER: WiFi connected successfully
I (1300) NETWORK_MANAGER: Got IP: 192.168.1.100
I (1350) NETWORK_MANAGER: NTP time synchronized
I (1400) I2S_HANDLER: I2S initialized successfully
I (1450) TCP_STREAMER: Connected successfully
I (1500) MAIN: I2S Reader task started
I (1550) MAIN: TCP Sender task started
I (1600) MAIN: Watchdog task started
I (1650) MAIN: === Audio Streamer Running ===
I (6000) MAIN: Buffer usage: 45%
I (10000) WATCHDOG: Stats: 5242880 bytes sent, 0 reconnects
```

### Error Conditions

**WiFi Connection Failure:**
```
E (5000) NETWORK_MANAGER: Failed to connect to WiFi
E (5010) MAIN: Failed to initialize WiFi, rebooting...
```

**TCP Connection Failure:**
```
E (3000) TCP_STREAMER: Failed to connect: errno 111
W (5000) TCP_STREAMER: Connection attempt 2/5 failed, retrying...
```

**Buffer Overflow:**
```
W (15000) BUFFER_MANAGER: Buffer overflow! Requested: 512, Available: 256
W (15010) MAIN: Buffer overflow detected!
```

## Testing

### Unit Tests

```bash
# Run tests
pio test --environment xiao_esp32s3
```

### Manual Testing

1. **I2S Signal Test**:
   - Use oscilloscope/logic analyzer
   - Verify SCK = 3.072 MHz (48 kHz × 64)
   - Verify WS = 48 kHz

2. **Network Test**:
   ```bash
   # From LXC container
   nc -l 9000
   # Should see binary audio data streaming
   ```

3. **Audio Quality Test**:
   - Record 1 minute segment
   - Analyze in Audacity
   - Check for dropouts, noise, distortion

## Troubleshooting

### Build Errors

**Error: `esp_idf_version.h` not found**
```bash
pio pkg update
```

**Error: PSRAM not detected**
- Check board configuration in `platformio.ini`
- Ensure using XIAO ESP32-S3, not ESP32-C3

### Runtime Errors

**WiFi won't connect**
- Verify SSID and password in `config.h`
- Check WiFi signal strength
- Try DHCP instead of static IP

**TCP connection fails**
- Verify server IP is reachable: `ping 192.168.1.50`
- Check server is listening: `nc -l 9000`
- Verify firewall allows port 9000

**Buffer overflows**
- Increase `RING_BUFFER_SIZE` in `config.h`
- Improve WiFi signal strength
- Check for network congestion

**I2S read errors**
- Verify wiring (SCK, WS, SD pins)
- Check INMP441 power (3.3V)
- Add decoupling capacitor

## Performance

### CPU Usage
- **Core 0**: 10-15% (WiFi stack + watchdog)
- **Core 1**: 30-40% (I2S + TCP)

### Memory Usage
- **SRAM**: ~50 KB (code + stacks)
- **PSRAM**: 512 KB (ring buffer)
- **Heap**: ~100 KB free

### Network Bandwidth
- **Theoretical**: 1.152 Mbps (48000 × 24)
- **Actual**: ~1.5 Mbps (TCP overhead)

## Optimization Tips

### Reduce CPU Load
```cpp
// Increase chunk size in tcp_sender_task
const size_t send_samples = 9600;  // 200ms chunks
```

### Reduce Memory Usage
```cpp
// Decrease ring buffer if stable network
#define RING_BUFFER_SIZE (256 * 1024)  // 256 KB
```

### Improve Reliability
```cpp
// Increase watchdog timeout for slow networks
#define WATCHDOG_TIMEOUT_SEC 60
```

## Alternative Boards

### ESP32-C3 Super Mini

Edit `platformio.ini` and select environment:
```bash
pio run --target upload --environment supermini_esp32c3
```

**Pin Configuration** for ESP32-C3:
```cpp
#define I2S_BCK_PIN 4
#define I2S_WS_PIN 5
#define I2S_DATA_IN_PIN 6
```

**Limitations**:
- Single core (no task pinning)
- No PSRAM (smaller buffer required)
- Lower max buffer size: 128 KB

## License

MIT License - See LICENSE file for details
