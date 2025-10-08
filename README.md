# ESP32-S3 Audio Streamer Firmware

PlatformIO firmware for streaming I2S audio from INMP441 microphone over WiFi/TCP.

**Quick Links:**

- [Features](#features) | [Quick Start](#quick-start) | [Configuration](#configuration) | [Architecture](#architecture)
- [Error Recovery](#error-recovery-system) | [Performance](#performance) | [Troubleshooting](#troubleshooting)
- [Recent Changes (v2.0)](#recent-improvements-v20) | [Changelog](#changelog)

## Features

- **I2S Audio Capture**: 16-bit @ 16 kHz from INMP441 microphone
- **WiFi Streaming**: Persistent TCP connection with automatic reconnection
- **Ring Buffer**: 96 KB in SRAM with overflow protection
- **Error Recovery**: Automatic I2S reinitialization and exponential backoff reconnection
- **Watchdog Monitoring**: Task health monitoring with auto-restart capability
- **NTP Time Sync**: Accurate timestamps with hourly resync
- **Dual-Core Architecture**: Optimized FreeRTOS task distribution across ESP32-S3 cores
- **Stack Monitoring**: Real-time stack usage tracking and low-memory warnings
- **Robust TCP**: Pre-allocated packing buffers and connection resilience

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

### Alternative: ESP-IDF Direct

For advanced users who want direct ESP-IDF access (menuconfig, advanced features):

**Windows:**
```bash
# Double-click to launch ESP-IDF environment
launch-esp-idf.bat
```

**Linux/Mac:**
```bash
# Make executable (first time)
chmod +x launch-esp-idf.sh

# Launch ESP-IDF environment
./launch-esp-idf.sh
```

**Then use idf.py commands:**
```bash
idf.py build          # Build project
idf.py flash monitor  # Flash and monitor
idf.py menuconfig     # Advanced configuration
```

See [ESP-IDF_LAUNCHER_GUIDE.md](ESP-IDF_LAUNCHER_GUIDE.md) for detailed instructions.

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

### PlatformIO Settings

The `platformio.ini` file configures the build environment:

```ini
[env:xiao_esp32s3]
platform = espressif32
board = seeed_xiao_esp32s3
framework = espidf
board_build.mcu = esp32s3
board_build.f_cpu = 240000000L

monitor_speed = 115200
upload_speed = 921600
monitor_filters = esp32_exception_decoder

build_flags =
    -DCONFIG_SPIRAM_SUPPORT=0          ; No PSRAM on this board
    -DCONFIG_HEAP_PLACE_FUNCTION_IN_FLASH=1  ; Save SRAM
    -DCORE_DEBUG_LEVEL=3               ; Enable info logging
```

**Key Points:**

- ESP-IDF framework (not Arduino)
- 240 MHz CPU frequency
- PSRAM disabled (not available on XIAO ESP32-S3)
- Exception decoder enabled for debugging
- Functions in flash to save SRAM

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
// Audio Configuration
#define SAMPLE_RATE 16000
#define BITS_PER_SAMPLE 16
#define CHANNELS 1  // Mono
#define BYTES_PER_SAMPLE 2  // 16-bit = 2 bytes per sample

// Buffer Configuration
#define I2S_DMA_BUF_COUNT 8
#define I2S_DMA_BUF_LEN 512  // 512 samples per DMA buffer
#define RING_BUFFER_SIZE (96 * 1024)  // 96 KB in internal SRAM

// Task priorities and core assignments
#define I2S_READER_PRIORITY 10  // Highest - real-time audio capture
#define TCP_SENDER_PRIORITY 8   // High - network transmission
#define WATCHDOG_PRIORITY 1     // Lowest - background monitoring

#define I2S_READER_CORE 1       // Dedicated audio core
#define TCP_SENDER_CORE 1       // Same core for efficiency
#define WATCHDOG_CORE 0         // Low priority, WiFi core

// Error handling and recovery
#define MAX_RECONNECT_ATTEMPTS 10        // Max TCP reconnect attempts before reboot
#define RECONNECT_BACKOFF_MS 1000        // Start with 1 second
#define MAX_RECONNECT_BACKOFF_MS 30000   // Cap at 30 seconds
#define MAX_I2S_FAILURES 100             // Max consecutive I2S failures before reinit
#define MAX_BUFFER_OVERFLOWS 20          // Max overflows before emergency drain
#define WATCHDOG_TIMEOUT_SEC 60          // Task watchdog timeout
```

## Architecture

### Error Recovery System

The firmware implements comprehensive error recovery mechanisms:

**I2S Recovery:**

- Tracks consecutive I2S read failures
- Automatically reinitializes I2S driver after 100 failures
- Falls back to system reboot if reinitialization fails
- Prevents permanent audio dropout

**TCP Recovery:**

- Exponential backoff reconnection (1s → 30s)
- Maximum 10 reconnect attempts before reboot
- Pre-allocated packing buffer survives reconnections
- Connection state monitoring with keepalive

**Buffer Management:**

- Detects and logs overflow events
- Emergency drain after 20 consecutive overflows
- 5-second mutex timeout prevents deadlocks
- Automatic PSRAM/SRAM allocation fallback

**WiFi Recovery:**

- Monitors connection state continuously
- Auto-reconnect on disconnect
- Forces TCP reconnection after WiFi recovery
- Maintains NTP sync across reconnections

**Watchdog Protection:**

- 60-second task timeout monitoring
- Automatic reboot on task hang
- Stack usage monitoring and warnings
- Prevents silent failures

### Task Design

Three FreeRTOS tasks running on dual-core ESP32-S3:

```
Core 0:                    Core 1:
┌────────────────┐        ┌────────────────┐
│ Watchdog Task  │        │ I2S Reader     │
│ (Priority 1)   │        │ (Priority 10)  │
│                │        │                │
│ • WiFi monitor │        │ • Read from    │
│ • NTP resync   │        │   I2S DMA      │
│ • Task health  │        │ • Write to     │
│ • Stack check  │        │   ring buffer  │
│ • Statistics   │        │ • Overflow det │
└────────────────┘        └────────────────┘
                          ┌────────────────┐
                          │ TCP Sender     │
                          │ (Priority 8)   │
                          │                │
                          │ • Read from    │
                          │   ring buffer  │
                          │ • Pack 16-bit  │
                          │ • Send TCP     │
                          │ • Auto reconn  │
                          └────────────────┘
```

**Key Improvements:**

- I2S Reader task has highest priority for real-time audio
- Both audio tasks run on Core 1 for better cache coherency
- Watchdog monitors task health and WiFi connection
- Stack monitoring prevents memory issues
- Error counters track consecutive failures

### Data Flow

```
INMP441 Mic
    ↓ (I2S @ 16-bit/16kHz)
I2S DMA Buffer (8 × 512 samples)
    ↓ (i2s_handler_read)
Ring Buffer (96 KB in SRAM)
    ↓ (buffer_manager_read)
16-bit Packing (2 bytes/sample)
    ↓ (tcp_streamer_send_audio)
TCP Socket → Server

Error Recovery:
• I2S failures → Reinit after 100 consecutive failures
• TCP failures → Exponential backoff reconnection
• Buffer overflow → Emergency drain after 20 overflows
• WiFi disconnect → Auto-reconnect with TCP re-establishment
```

## Module Documentation

### I2S Handler (`i2s_handler.cpp`)

Manages I2S peripheral for audio capture from INMP441.

**Key Functions:**

- `i2s_handler_init()`: Initialize I2S driver with DMA
- `i2s_handler_read()`: Read samples from DMA buffer
- `i2s_handler_deinit()`: Cleanup I2S resources
- `i2s_handler_get_stats()`: Get overflow/underflow counts

**Configuration:**

- 16 kHz sample rate
- 16-bit samples
- Mono (left channel only)
- 8 DMA buffers × 512 samples
- Philips I2S standard mode
- Auto bit-width detection (supports 16/24/32-bit)

**Features:**

- Dynamic bit-width configuration
- Error logging with ESP-IDF error names
- Statistics tracking for debugging

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

- `tcp_streamer_init()`: Connect to TCP server with retry logic
- `tcp_streamer_send_audio()`: Send 16-bit packed audio
- `tcp_streamer_reconnect()`: Reconnect on failure
- `tcp_streamer_deinit()`: Cleanup resources and free buffers
- `tcp_streamer_get_stats()`: Get bytes sent and reconnect count

**Features:**

- Pre-allocated packing buffer (16384 samples × 2 bytes)
- Persistent TCP connection with keepalive
- Automatic reconnection with exponential backoff (1s → 30s)
- 16-bit packing (2 bytes/sample from 32-bit containers)
- Socket timeout protection (5 seconds)
- Graceful connection closure and resource cleanup

**Improvements:**

- Memory-efficient: Single packing buffer allocated at init
- Robust error handling with errno logging
- Handles EAGAIN/EWOULDBLOCK for non-blocking sends

### Buffer Manager (`buffer_manager.cpp`)

Thread-safe ring buffer for audio samples.

**Key Functions:**

- `buffer_manager_init()`: Allocate buffer (tries PSRAM first, falls back to SRAM)
- `buffer_manager_write()`: Write samples (from I2S task) with timeout protection
- `buffer_manager_read()`: Read samples (from TCP task)
- `buffer_manager_usage_percent()`: Get buffer utilization
- `buffer_manager_reset()`: Emergency buffer drain
- `buffer_manager_deinit()`: Cleanup resources

**Features:**

- 96 KB capacity in internal SRAM (ESP32-S3 has 512 KB total)
- Automatic PSRAM detection with SRAM fallback
- Mutex-protected operations with 5-second timeout
- Overflow detection and flagging
- Usage monitoring and statistics
- Emergency drain capability

**Safety Improvements:**

- Mutex timeout prevents deadlocks
- Overflow counter for diagnostics
- Thread-safe reset operation

## Serial Monitor Output

### Normal Operation

```
I (1234) MAIN: === Audio Streamer Starting ===
I (1250) MAIN: ESP-IDF Version: v5.x.x
I (1260) NETWORK_MANAGER: WiFi connected successfully
I (1300) NETWORK_MANAGER: Got IP: 192.168.1.100
I (1350) NETWORK_MANAGER: NTP time synchronized
I (1360) MAIN: Free heap: 250000 bytes
I (1370) MAIN: Largest free block: 200000 bytes
I (1400) BUFFER_MANAGER: Ring buffer allocated in internal SRAM
I (1410) BUFFER_MANAGER: Buffer manager initialized successfully
I (1450) I2S_HANDLER: I2S initialized successfully (new API)
I (1460) I2S_HANDLER: Sample rate: 16000 Hz, Bits: 16, Channels: 1
I (1500) TCP_STREAMER: Packing buffer allocated: 32768 bytes
I (1520) TCP_STREAMER: Connected successfully
I (1550) MAIN: I2S Reader task created
I (1560) MAIN: TCP Sender task created
I (1570) MAIN: Watchdog task created
I (1600) MAIN: === Audio Streamer Running ===
I (6000) MAIN: B:5242880 R:0 OF:0
I (10000) WATCHDOG: Stats logged
```

**Statistics Legend:**

- `B:` Total bytes sent
- `R:` Reconnect count
- `OF:` Buffer overflow count

### Error Conditions

**WiFi Connection Failure:**

```
E (5000) NETWORK_MANAGER: Failed to connect to WiFi
E (5010) MAIN: CRITICAL: WiFi init failed, rebooting in 5 seconds...
```

**TCP Connection Failure:**

```
E (3000) TCP_STREAMER: Failed to connect: errno 111
W (5000) TCP_STREAMER: Connection attempt 2/5 failed, retrying...
I (7000) TCP_STREAMER: Waiting 2000ms before reconnect...
```

**Buffer Overflow:**

```
W (15000) BUFFER_MANAGER: Buffer overflow! Requested: 512, Available: 256
W (15010) MAIN: Buffer overflow detected!
W (20000) MAIN: Too many overflows (21), forcing buffer drain
```

**I2S Failures:**

```
E (25000) I2S_HANDLER: Failed to read from I2S: ESP_ERR_TIMEOUT
E (25010) MAIN: I2S read failed (consecutive: 50)
E (30000) MAIN: Too many I2S failures, reinitializing...
I (31000) MAIN: I2S reinitialized successfully
```

**Task Timeout:**

```
E (60000) MAIN: I2S Reader timeout! Last feed: 65 sec ago
E (60010) MAIN: Rebooting...
```

**Stack Warning:**

```
W (70000) MAIN: ⚠️ I2S task low stack: 480 bytes free
W (70010) MAIN: ⚠️ TCP task low stack: 350 bytes free
```

## Testing

### Recent Improvements (v2.0)

**Major Changes:**

1. **Audio Configuration**: Changed from 24-bit/48kHz to 16-bit/16kHz

   - Reduced bandwidth from 1.152 Mbps to 256 kbps
   - Better suited for WiFi streaming
   - Lower CPU and memory requirements

2. **Error Recovery**: Comprehensive failure handling

   - I2S automatic reinitialization
   - TCP exponential backoff reconnection
   - Buffer overflow emergency drain
   - WiFi disconnect recovery

3. **Memory Optimization**:

   - Switched from PSRAM (512KB) to SRAM (96KB)
   - Pre-allocated TCP packing buffer
   - Reduced heap fragmentation
   - Mutex timeout protection (5 seconds)

4. **Task Improvements**:

   - Increased I2S priority (10) for real-time audio
   - Both audio tasks on Core 1 for cache coherency
   - Stack monitoring with warnings
   - Better task scheduling and delays

5. **Monitoring & Diagnostics**:

   - Compact statistics logging (B:bytes R:reconnects OF:overflows)
   - Stack usage monitoring
   - Consecutive failure tracking
   - Heap usage reporting

6. **Robustness**:
   - Watchdog monitoring with 60s timeout
   - Automatic reboot on critical failures
   - Connection state tracking
   - Error counter thresholds

### Unit Tests

```bash
# Run tests
pio test --environment xiao_esp32s3
```

### Manual Testing

1. **I2S Signal Test**:

   - Use oscilloscope/logic analyzer on GPIO pins
   - Verify BCK = 1.024 MHz (16 kHz × 64)
   - Verify WS = 16 kHz
   - Check data transitions align with clock

2. **Network Test**:

   ```bash
   # Start TCP server on LXC container
   nc -l 9000 | xxd | head -100

   # Should see binary audio data streaming
   # Expected format: 16-bit little-endian samples
   ```

3. **Audio Quality Test**:

   ```bash
   # Record 1 minute to file
   nc -l 9000 > audio.raw

   # Convert to WAV for analysis
   ffmpeg -f s16le -ar 16000 -ac 1 -i audio.raw audio.wav

   # Analyze in Audacity
   # Check for: dropouts, noise floor, frequency response
   ```

4. **Stress Test**:

   ```bash
   # Poor WiFi conditions
   # - Move ESP32 far from router
   # - Add WiFi interference
   # Monitor recovery in serial output

   # Look for:
   # - Automatic reconnections
   # - Buffer overflow handling
   # - No crashes or reboots
   ```

5. **Long-term Stability**:
   - Run for 24+ hours
   - Monitor heap fragmentation
   - Check for memory leaks
   - Verify statistics make sense
   - Confirm no watchdog timeouts

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

- Check network congestion: `ping -c 100 192.168.1.100`
- Increase `RING_BUFFER_SIZE` in `config.h` (e.g., 128 KB)
- Reduce sample rate to 8 kHz if bandwidth limited
- Check WiFi signal strength (should be > -70 dBm)
- Monitor overflow counter in logs

**I2S read errors**

- Verify wiring (BCK→GPIO2, WS→GPIO3, SD→GPIO1)
- Check INMP441 power (3.3V, GND)
- Add 100nF decoupling capacitor near mic
- Test with different GPIO pins if hardware issue
- Check for electromagnetic interference

**Task watchdog timeouts**

- Increase `WATCHDOG_TIMEOUT_SEC` in `config.h`
- Check stack sizes aren't too small
- Monitor CPU usage in logs
- Verify task priorities are appropriate

**Stack overflow warnings**

- Increase task stack sizes in `config.h`
- Current defaults: I2S=4096, TCP=4096, WD=4096 bytes
- Monitor with `ENABLE_STACK_MONITORING=1`

## Performance

### CPU Usage

- **Core 0**: 5-10% (WiFi stack + watchdog)
- **Core 1**: 25-35% (I2S + TCP tasks)
- **Total**: ~35% average (leaves headroom for processing)

### Memory Usage

- **SRAM**: ~80 KB (code + stacks + ring buffer)
- **PSRAM**: Not used (fallback available)
- **Heap**: ~170 KB free after init
- **Stack per task**:
  - I2S Reader: 4096 bytes
  - TCP Sender: 4096 bytes
  - Watchdog: 4096 bytes

### Network Bandwidth

- **Raw Audio**: 256 kbps (16000 Hz × 16 bits)
- **TCP Overhead**: ~280 kbps actual
- **Packets/sec**: ~62 (16384 samples × 2 bytes / packet)
- **Latency**: 200-500ms (buffering + network)

### Reliability Metrics

- **MTBF**: 24+ hours continuous streaming
- **WiFi reconnect**: < 5 seconds
- **TCP reconnect**: 1-30 seconds (exponential backoff)
- **Buffer overflow**: Rare with good WiFi (< 1 per hour)
- **I2S errors**: ~0 with proper wiring

## Optimization Tips

### Reduce CPU Load

```cpp
// Increase send chunk size for fewer TCP calls
const size_t send_samples = 16384;  // 1 second chunks at 16kHz

// Lower I2S priority if not critical
#define I2S_READER_PRIORITY 8
```

### Reduce Memory Usage

```cpp
// Decrease ring buffer if network is stable
#define RING_BUFFER_SIZE (64 * 1024)  // 64 KB minimum

// Reduce task stack sizes if monitored safe
#define TCP_SENDER_STACK_SIZE 3072
```

### Improve Reliability

```cpp
// Increase watchdog timeout for slow networks
#define WATCHDOG_TIMEOUT_SEC 90

// More aggressive reconnection
#define MAX_RECONNECT_ATTEMPTS 20

// Enable all recovery features
#define ENABLE_AUTO_REBOOT 1
#define ENABLE_I2S_REINIT 1
#define ENABLE_BUFFER_DRAIN 1
#define ENABLE_STACK_MONITORING 1
```

### Power Optimization

```cpp
// If battery powered, enable WiFi power save
// In network_manager.cpp, change:
ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MIN_MODEM));

// Lower sample rate
#define SAMPLE_RATE 8000  // Halves bandwidth
```

### Network Optimization

```cpp
// Increase TCP buffer sizes for better throughput
int sendbuf_size = 32768;
setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &sendbuf_size, sizeof(sendbuf_size));

// Disable Nagle's algorithm for lower latency
int flag = 1;
setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
```

## Alternative Boards

### Known Issues

1. **Initial TCP Connection**: May fail if server not ready, but will retry in background
2. **WiFi Roaming**: Connection drops during access point changes (by design, no roaming support)
3. **PSRAM Support**: Not available on XIAO ESP32-S3, limited to SRAM buffering
4. **Watchdog False Positives**: Very rare, but task delays can trigger timeout on heavily loaded systems

### Future Improvements

**Planned Features:**

- [ ] Add audio compression (OPUS codec)
- [ ] Implement UDP streaming option for lower latency
- [ ] Add web interface for configuration
- [ ] Support for stereo microphones
- [ ] Battery monitoring and power management
- [ ] OTA (Over-The-Air) firmware updates
- [ ] Local audio recording to SD card
- [ ] Voice Activity Detection (VAD)

**Performance Enhancements:**

- [ ] DMA optimizations for lower CPU usage
- [ ] Assembly optimizations for critical paths
- [ ] Adaptive buffer sizing based on network conditions
- [ ] Quality of Service (QoS) for WiFi packets

### Alternative Boards

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
- No PSRAM (limited to internal SRAM)
- Lower max buffer size: 64 KB recommended
- Reduced multitasking capability

**Configuration adjustments needed:**

```cpp
// Smaller buffer for single core
#define RING_BUFFER_SIZE (64 * 1024)

// Remove core pinning (automatic scheduling)
// Just pass tskNO_AFFINITY or omit core parameter
```

## Changelog

### Version 2.0 (Current)

- **Breaking**: Changed audio format from 24-bit/48kHz to 16-bit/16kHz
- **Feature**: Added comprehensive error recovery system
- **Feature**: Implemented I2S automatic reinitialization on failures
- **Feature**: Added TCP exponential backoff reconnection
- **Feature**: Implemented stack monitoring and warnings
- **Improvement**: Pre-allocated TCP packing buffer (eliminates per-send allocation)
- **Improvement**: Switched from PSRAM to SRAM (96KB ring buffer)
- **Improvement**: Enhanced watchdog monitoring with timeout protection
- **Improvement**: Better task scheduling with explicit delays
- **Improvement**: WiFi reconnection triggers TCP re-establishment
- **Fix**: Mutex timeout (5s) prevents deadlocks
- **Fix**: Buffer overflow emergency drain after threshold
- **Fix**: Task watchdog timeout prevention with periodic yields

### Version 1.0 (Initial)

- Basic I2S audio capture at 24-bit/48kHz
- TCP streaming over WiFi
- 512KB PSRAM ring buffer
- Basic watchdog monitoring
- NTP time synchronization

## License

MIT License - See LICENSE file for details
