# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ESP32-S3 firmware for streaming I2S audio from INMP441 microphone over WiFi/TCP using ESP-IDF framework. Dual-build system supports both PlatformIO (recommended for quick iterations) and native ESP-IDF (for advanced configuration).

**Target Hardware**: Seeed XIAO ESP32-S3 (240MHz dual-core, 512KB SRAM, no PSRAM)

## Build Commands

### PlatformIO (Primary Method)
```bash
# Build and flash
pio run --target upload --environment xiao_esp32s3

# Monitor serial output with colorization and exception decoder
pio device monitor

# Clean build
pio run --target clean

# Run tests
pio test --environment xiao_esp32s3
```

### ESP-IDF (Advanced Method)
```bash
# Windows: Launch ESP-IDF environment
launch-esp-idf.bat

# Linux/Mac: Launch ESP-IDF environment
chmod +x launch-esp-idf.sh
./launch-esp-idf.sh

# Then use idf.py commands:
idf.py build
idf.py flash monitor
idf.py menuconfig  # Advanced configuration
```

## Architecture Overview

### Three-Layer Module Design

**Core Audio Chain** (`src/modules/`):
- `i2s_handler` → Manages I2S peripheral and DMA for INMP441 microphone capture
- `buffer_manager` → Thread-safe 96KB ring buffer in SRAM with mutex protection
- `tcp_streamer` → TCP client with exponential backoff reconnection and 16-bit packing

**Infrastructure** (`src/modules/`):
- `network_manager` → WiFi connection, NTP sync (hourly), static IP or DHCP
- `config_manager` → NVS-backed configuration persistence across reboots
- `web_server` → Embedded HTTP server with REST API for configuration

**Task Orchestration** (`src/main.cpp`):
Three FreeRTOS tasks with priority-based scheduling:
```
Core 0: Watchdog (P=1)      → WiFi monitoring, NTP resync, health checks
Core 1: I2S Reader (P=10)   → Real-time audio capture (highest priority)
Core 1: TCP Sender (P=8)    → Network transmission with packing
```

### Critical Data Flow

```
INMP441 Mic (I2S 16-bit/16kHz mono)
    ↓
I2S DMA (8 buffers × 512 samples = 32ms buffering)
    ↓
i2s_handler_read() → 32-bit containers from DMA
    ↓
Ring Buffer (96KB SRAM = ~3 seconds audio buffering)
    ↓ mutex-protected read/write with 5s timeout
buffer_manager_read() → extract samples
    ↓
16-bit packing (2 bytes/sample from 32-bit containers)
    ↓
tcp_streamer_send_audio() → persistent TCP connection
    ↓
TCP Server (192.168.1.50:9000 by default)
```

### Error Recovery Architecture

**Multi-Level Failure Handling**:

1. **I2S Layer** (`src/modules/i2s_handler.cpp`):
   - Tracks consecutive read failures
   - Auto-reinitializes I2S driver after 100 failures (configurable: MAX_I2S_FAILURES)
   - Falls back to system reboot if reinitialization fails

2. **Network Layer** (`src/modules/tcp_streamer.cpp`):
   - Exponential backoff: 1s → 2s → 4s → ... → 30s max
   - Pre-allocated packing buffer survives reconnections (eliminates heap churn)
   - Max 10 reconnect attempts before system reboot (configurable: MAX_RECONNECT_ATTEMPTS)

3. **Buffer Layer** (`src/modules/buffer_manager.cpp`):
   - Mutex timeout prevents deadlocks (5s, configurable: MUTEX_TIMEOUT_MS)
   - Emergency drain after 20 consecutive overflows (configurable: MAX_BUFFER_OVERFLOWS)
   - Automatic PSRAM fallback to SRAM if PSRAM unavailable

4. **Task Watchdog** (`src/main.cpp`):
   - 60-second timeout per task (configurable: WATCHDOG_TIMEOUT_SEC)
   - Automatic reboot on task hang
   - Stack monitoring with low-water-mark warnings (<512 bytes free)

## Configuration Files

### `src/config.h` - Central Configuration Hub

**WiFi Credentials** (edit before first build):
```cpp
#define WIFI_SSID "your_wifi"
#define WIFI_PASSWORD "your_password"
#define STATIC_IP_ADDR "0.0.0.0"  // Use "0.0.0.0" for DHCP
```

**TCP Server** (edit to match your receiving server):
```cpp
#define TCP_SERVER_IP "192.168.1.50"  // Server IP address
#define TCP_SERVER_PORT 9000           // TCP port
```

**I2S Pin Assignments** (GPIO mapping for XIAO ESP32-S3):
```cpp
#define I2S_BCK_PIN 2      // Bit Clock
#define I2S_WS_PIN 3       // Word Select (LRCLK)
#define I2S_DATA_IN_PIN 1  // Serial Data In
```

**Audio Configuration**:
```cpp
#define SAMPLE_RATE 16000       // 16 kHz sample rate (256 kbps bandwidth)
#define BITS_PER_SAMPLE 16      // 16-bit samples
#define CHANNELS 1              // Mono audio
#define I2S_DMA_BUF_COUNT 8     // 8 DMA buffers
#define I2S_DMA_BUF_LEN 512     // 512 samples per buffer (32ms total)
```

**Memory Configuration**:
```cpp
#define RING_BUFFER_SIZE (96 * 1024)  // 96KB buffer (~6 seconds at 16kHz)
```

**Task Configuration** (careful: affects real-time behavior):
```cpp
// Priorities (0=lowest, 25=highest typically)
#define I2S_READER_PRIORITY 10  // Highest for real-time capture
#define TCP_SENDER_PRIORITY 8   // Medium for network transmission
#define WATCHDOG_PRIORITY 1     // Lowest for background monitoring

// Core assignments (0 or 1)
#define I2S_READER_CORE 1       // Dedicated audio core
#define TCP_SENDER_CORE 1       // Same core for cache coherency
#define WATCHDOG_CORE 0         // Low-priority, shares with WiFi stack
```

**Recovery Thresholds** (tuning for stability vs responsiveness):
```cpp
#define MAX_I2S_FAILURES 100              // Consecutive I2S failures before reinit
#define MAX_RECONNECT_ATTEMPTS 10         // TCP reconnects before reboot
#define MAX_BUFFER_OVERFLOWS 20           // Overflows before emergency drain
#define RECONNECT_BACKOFF_MS 1000         // Initial backoff (1s)
#define MAX_RECONNECT_BACKOFF_MS 30000    // Max backoff (30s)
```

### `platformio.ini` - Build Configuration

**Key Build Flags**:
- `CONFIG_SPIRAM_SUPPORT=0` - No PSRAM on XIAO ESP32-S3
- `CONFIG_HEAP_PLACE_FUNCTION_IN_FLASH=1` - Save SRAM by placing functions in flash
- `CORE_DEBUG_LEVEL=3` - Info-level logging (3=INFO, 4=DEBUG, 5=VERBOSE)

**Monitoring**:
- `monitor_speed = 115200` - Serial baud rate
- `monitor_filters = esp32_exception_decoder` - Automatic stack trace decoding

## Web UI and REST API

### Accessing the Web Interface

1. Device connects to WiFi using credentials in `src/config.h` or stored in NVS
2. Check serial monitor for IP address assignment
3. Navigate to `http://[DEVICE_IP]` in browser

### REST API Endpoints (all in `src/modules/web_server.cpp`)

**Configuration** (GET returns current, POST updates and persists to NVS):
- `/api/config/wifi` - WiFi SSID, password, static IP
- `/api/config/tcp` - Server IP and port
- `/api/config/audio` - Sample rate, bit depth, channels
- `/api/config/buffer` - Ring buffer size
- `/api/config/tasks` - Task priorities and core assignments
- `/api/config/error` - Recovery thresholds
- `/api/config/debug` - Debug flags and monitoring
- `/api/config/all` - Get all configuration as JSON

**System Control**:
- `GET /api/system/status` - Uptime, memory, task status
- `GET /api/system/info` - Chip model, IDF version, MAC address
- `POST /api/system/restart` - Restart device
- `POST /api/system/factory-reset` - Reset configuration to defaults
- `POST /api/system/save` - Persist current config to NVS
- `POST /api/system/load` - Reload config from NVS

**Web Assets** (embedded in firmware, no SPIFFS):
- `data/index.html` - Dashboard with auto-refresh
- `data/config.html` - Configuration forms
- `data/monitor.html` - Real-time monitoring
- `data/css/style.css` - Responsive UI styling
- `data/js/` - API client, utilities, page logic

## Modular Architecture Details

### Module Initialization Sequence (src/main.cpp:app_main())

```cpp
1. network_manager_init()      → WiFi connection, NTP sync
2. config_manager_init()       → Load NVS configuration
3. buffer_manager_init()       → Allocate ring buffer (PSRAM → SRAM fallback)
4. i2s_handler_init()          → I2S peripheral setup with DMA
5. tcp_streamer_init()         → TCP connection with retry
6. web_server_init()           → Start HTTP server on port 80
7. xTaskCreatePinnedToCore()   → Spawn three FreeRTOS tasks
```

### Module Interfaces

**i2s_handler.h**:
```cpp
bool i2s_handler_init(void);
bool i2s_handler_read(int32_t* buffer, size_t samples, size_t* bytes_read);
void i2s_handler_deinit(void);
void i2s_handler_get_stats(uint32_t* overflows, uint32_t* underflows);
```

**buffer_manager.h**:
```cpp
bool buffer_manager_init(void);
size_t buffer_manager_write(const int32_t* data, size_t samples);
size_t buffer_manager_read(int32_t* data, size_t samples);
uint8_t buffer_manager_usage_percent(void);
void buffer_manager_reset(void);  // Emergency drain
void buffer_manager_deinit(void);
```

**tcp_streamer.h**:
```cpp
bool tcp_streamer_init(void);
bool tcp_streamer_send_audio(const int32_t* samples, size_t count);
bool tcp_streamer_reconnect(void);
void tcp_streamer_deinit(void);
void tcp_streamer_get_stats(uint64_t* bytes_sent, uint32_t* reconnect_count);
```

**network_manager.h**:
```cpp
bool network_manager_init(void);
bool network_manager_is_connected(void);
bool network_manager_init_ntp(void);
bool network_manager_resync_ntp(void);
```

**config_manager.h**:
```cpp
bool config_manager_init(void);
bool config_manager_load(void);
bool config_manager_save(void);
bool config_manager_reset(void);
esp_err_t config_manager_get_wifi_config(wifi_config_t* config);
esp_err_t config_manager_set_wifi_config(const wifi_config_t* config);
// ... similar getters/setters for TCP, audio, buffer, task configs
```

**web_server.h**:
```cpp
bool web_server_init(void);
void web_server_deinit(void);
// HTTP handlers registered internally
```

## Development Patterns and Conventions

### Error Handling Pattern

All module initialization returns `bool` (true=success, false=failure):

```cpp
if (!module_init()) {
    ESP_LOGE(TAG, "Module init failed");
    esp_restart();  // Critical failure → reboot
    return;
}
```

### Logging Conventions

- Use ESP-IDF logging macros: `ESP_LOGE`, `ESP_LOGW`, `ESP_LOGI`, `ESP_LOGD`, `ESP_LOGV`
- Tag format: Module name in uppercase (e.g., `static const char* TAG = "I2S_HANDLER";`)
- Log levels controlled by `CORE_DEBUG_LEVEL` in `platformio.ini`

### Memory Management

- **Stack**: Tasks use 4KB stacks (I2S, TCP, Watchdog)
- **Heap**: Minimize allocations in real-time tasks
- **Pre-allocation**: TCP packing buffer allocated once at init (32KB)
- **Ring buffer**: 96KB allocated at startup (PSRAM first, SRAM fallback)

### Thread Safety

- Ring buffer uses FreeRTOS mutex with 5-second timeout
- I2S task writes to ring buffer
- TCP task reads from ring buffer
- No shared state between tasks except through buffer_manager

## Testing Patterns

### Manual I2S Signal Verification

```bash
# Use logic analyzer or oscilloscope on XIAO ESP32-S3 GPIO pins:
# BCK (GPIO2): Should show 1.024 MHz (16kHz × 64)
# WS (GPIO3): Should show 16 kHz square wave
# DATA (GPIO1): Should show serial data transitions aligned with BCK
```

### Network Reception Test

```bash
# Start TCP server on Linux/Mac:
nc -l 9000 | xxd | head -100

# Expected: Binary audio data streaming (16-bit little-endian samples)
```

### Audio Quality Validation

```bash
# Record 60 seconds to file:
nc -l 9000 > audio.raw

# Convert to WAV:
ffmpeg -f s16le -ar 16000 -ac 1 -i audio.raw audio.wav

# Analyze in Audacity:
# - Check frequency response (should be flat 20Hz-8kHz for 16kHz sampling)
# - Verify no dropouts or discontinuities
# - Measure noise floor
```

### Long-term Stability Test

Run for 24+ hours while monitoring:
- Heap fragmentation (should remain stable)
- Reconnection count (should be zero with good WiFi)
- Buffer overflow count (should be zero or very low)
- Stack watermarks (should stay above 512 bytes)

## Common Development Tasks

### Changing Audio Parameters

1. Edit `src/config.h`: Modify `SAMPLE_RATE`, `BITS_PER_SAMPLE`, or `CHANNELS`
2. Rebuild and flash (config changes require recompilation)
3. Update TCP server to match new audio format

### Adding New Configuration Parameters

1. Add getter/setter to `config_manager.h/cpp`
2. Add NVS storage in `config_manager_load()` and `config_manager_save()`
3. Add REST API endpoint in `web_server.cpp`
4. Update web UI forms in `data/config.html` and `data/js/config.js`

### Debugging Task Issues

Enable detailed monitoring in `src/config.h`:
```cpp
#define ENABLE_STACK_MONITORING 1
#define CORE_DEBUG_LEVEL 4  // In platformio.ini
```

Monitor serial output for:
- Task stack watermarks (logged every 10 seconds by watchdog)
- Task feed times (watchdog tracks last activity)
- Consecutive failure counters

### Optimizing for Different Networks

**Good WiFi (low latency, high bandwidth)**:
- Reduce `RING_BUFFER_SIZE` to 64KB (saves 32KB SRAM)
- Increase `SAMPLE_RATE` to 24000 or 48000 for higher quality

**Poor WiFi (high latency, packet loss)**:
- Increase `RING_BUFFER_SIZE` to 128KB (more buffering)
- Increase `MAX_RECONNECT_BACKOFF_MS` to 60000 (60s max backoff)
- Consider implementing UDP streaming for lower latency

## Troubleshooting Common Issues

### "Buffer overflow! Requested: 512, Available: 256"

**Root Cause**: TCP sending slower than I2S capture (network congestion)

**Solutions**:
1. Check WiFi signal strength: `ping -c 100 192.168.1.100` (should be <10ms latency)
2. Increase `RING_BUFFER_SIZE` to 128KB in `src/config.h`
3. Verify TCP server is consuming data (not blocking)
4. Reduce `SAMPLE_RATE` to 8000 if bandwidth-limited

### "I2S read failed (consecutive: 50)"

**Root Cause**: Hardware wiring issue or I2S clock failure

**Solutions**:
1. Verify wiring: BCK→GPIO2, WS→GPIO3, SD→GPIO1, VCC→3.3V, GND→GND
2. Check INMP441 power with multimeter (should be 3.3V ±5%)
3. Add 100nF decoupling capacitor near microphone
4. Test with different GPIO pins (update `src/config.h`)
5. Check for electromagnetic interference from WiFi antenna

### "WiFi connected successfully" but "Failed to connect: errno 111"

**Root Cause**: TCP server not reachable (ECONNREFUSED)

**Solutions**:
1. Verify server IP reachable: `ping 192.168.1.50`
2. Check server is listening: `nc -l 9000` (should block waiting for connection)
3. Verify firewall allows port 9000 inbound
4. Check routing if server on different subnet

### Task watchdog timeout

**Root Cause**: Task blocked longer than 60 seconds

**Solutions**:
1. Increase `WATCHDOG_TIMEOUT_SEC` in `src/config.h` (try 90 or 120)
2. Check for mutex deadlocks (mutex timeout should prevent this)
3. Verify task priorities aren't preventing task execution
4. Monitor CPU usage (should be <50% average)

## Performance Characteristics

### CPU Utilization (measured at 16kHz/16-bit mono)

- Core 0: 5-10% (WiFi stack, watchdog, web server)
- Core 1: 25-35% (I2S DMA handling, TCP packing and transmission)
- **Total**: ~35% average (leaves 65% headroom for processing)

### Memory Footprint

- **Code + rodata**: ~320KB flash
- **SRAM usage**: ~80KB (stacks 12KB, ring buffer 96KB, heap ~20KB)
- **Free heap after init**: ~170KB
- **Web assets**: ~36KB flash (embedded, not in SPIFFS)

### Network Bandwidth

- **Raw audio**: 256 kbps (16000 samples/sec × 16 bits)
- **TCP overhead**: ~280 kbps actual (headers, ACKs, retransmits)
- **Packets per second**: ~62 (16384 samples × 2 bytes per packet)
- **Latency**: 200-500ms (DMA buffering + ring buffer + network)

### Reliability Metrics (field-tested)

- **MTBF**: 24+ hours continuous streaming (limited by network stability)
- **WiFi reconnect time**: <5 seconds
- **TCP reconnect time**: 1-30 seconds (exponential backoff)
- **Buffer overflow rate**: <1 per hour with good WiFi signal (>-70 dBm)
- **I2S error rate**: ~0 with proper wiring

## Important Notes for Code Modifications

### DO NOT modify without understanding impact:

1. **Task Priorities**: I2S_READER_PRIORITY must be highest to avoid audio dropouts
2. **Core Assignments**: I2S and TCP on same core for cache coherency (data sharing)
3. **Ring Buffer Size**: Must fit in available SRAM (ESP32-S3 has 512KB total)
4. **Mutex Timeouts**: 5-second timeout prevents deadlocks but may cause data loss under extreme conditions
5. **DMA Buffer Count**: Affects I2S interrupt frequency (8 buffers × 512 samples = 32ms)

### Memory Constraints:

- ESP32-S3 has 512KB SRAM total, ~170KB free after init
- Increasing `RING_BUFFER_SIZE` beyond 128KB risks OOM errors
- Task stack sizes are minimums; monitor watermarks before reducing
- Web assets embedded in flash, not SPIFFS (saves flash space)

### Real-time Considerations:

- I2S task must execute every 32ms to drain DMA buffers (8 × 512 samples @ 16kHz)
- TCP task should send every ~1 second (16384 samples = 1s @ 16kHz)
- Watchdog task runs every 5 seconds (non-critical)
- WiFi stack runs on Core 0, avoid blocking operations there

## Alternative Hardware

### ESP32-C3 Super Mini (single-core variant)

**Pin Configuration**:
```cpp
#define I2S_BCK_PIN 4
#define I2S_WS_PIN 5
#define I2S_DATA_IN_PIN 6
```

**Required Changes in src/config.h**:
```cpp
#define RING_BUFFER_SIZE (64 * 1024)  // Smaller buffer for single core
// Remove core pinning: Use tskNO_AFFINITY or omit core parameter
```

**Limitations**:
- Single core: No task pinning, reduced multitasking capability
- No PSRAM: Limited to internal SRAM only
- Lower concurrent performance: WiFi + I2S + TCP compete for CPU time

## Build System Details

### Dual Build System Architecture

- **PlatformIO**: Wraps ESP-IDF for easier dependency management and IDE integration
- **ESP-IDF native**: Direct access to menuconfig and advanced features
- Both use same source code in `src/` directory

### CMake Structure

- Root `CMakeLists.txt`: Defines project and sets `EXTRA_COMPONENT_DIRS = src`
- `src/CMakeLists.txt`: Defines component, embeds web assets via `target_add_binary_data()`
- Web assets in `data/` directory compiled into firmware binary

### Custom Build Scripts

- `disable_component_manager.py`: Workaround for ESP-IDF 5.5.0 pydantic bug
- `launch-esp-idf.{bat,sh,ps1}`: Launch ESP-IDF environment with IDF_PATH setup
- `colorize.py`: Serial monitor output colorization (optional)

## Configuration Persistence (NVS)

All runtime-modifiable parameters stored in Non-Volatile Storage (NVS):

**Namespace**: `audio_config`

**Keys** (stored in `config_manager.cpp`):
- `wifi_ssid`, `wifi_pass`, `static_ip`, `gateway`, `subnet`
- `tcp_ip`, `tcp_port`
- `sample_rate`, `bits_per_sample`, `channels`
- `buffer_size`
- `i2s_prio`, `tcp_prio`, `watchdog_prio`
- `i2s_core`, `tcp_core`, `watchdog_core`
- `max_i2s_fail`, `max_reconnect`, `max_overflow`

**Factory Reset**: Clears entire `audio_config` namespace, device reverts to `src/config.h` defaults

## Serial Monitor Output Interpretation

**Statistics Line** (logged every 5 seconds):
```
I (6000) MAIN: B:5242880 R:0 OF:0
```
- `B`: Total bytes sent over TCP
- `R`: Reconnection count since boot
- `OF`: Buffer overflow count

**Stack Watermarks** (logged every 10 seconds):
```
I (10000) WATCHDOG: I2S stack: 1200 bytes free
```
- Warning threshold: 512 bytes (MIN_STACK_WATERMARK)
- If consistently <512, increase stack size in `src/config.h`

## Future Enhancement Considerations

**Planned but not implemented**:
- Audio compression (OPUS codec) - would reduce bandwidth to ~32 kbps
- UDP streaming option - would reduce latency to <100ms
- OTA firmware updates - would enable wireless updates
- Local SD card recording - would enable offline operation
- Voice Activity Detection (VAD) - would reduce bandwidth during silence
- Captive portal - would simplify first-time configuration
- HTTP authentication - would secure web UI access
- WebSocket support - would enable true real-time web UI updates

## Security Considerations

**Current State**: No authentication, HTTP only (not HTTPS)

**Suitable for**: Trusted private networks only

**DO NOT expose to internet** without implementing:
1. HTTP Basic Authentication or session-based auth
2. HTTPS with self-signed or Let's Encrypt certificates
3. Rate limiting on API endpoints
4. Input validation and sanitization (partially implemented)

## License

MIT License - See LICENSE file for details
