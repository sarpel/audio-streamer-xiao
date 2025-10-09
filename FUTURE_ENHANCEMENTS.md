# Future Enhancements

This document tracks features and improvements that were considered but deferred for future implementation. Items are organized by priority and complexity.

## Deferred from Code Review (2025-10-09)

### Security Enhancements

#### Password Hashing (Medium Priority)
**Status**: Deferred - Optional enhancement  
**Effort**: 2-3 hours  
**Benefit**: Improved security for credentials stored in NVS

**Current State**: Passwords stored in plaintext in NVS

**Proposed Implementation**:
```cpp
// Use SHA256 for password hashing
#include "mbedtls/sha256.h"

bool hash_password(const char* password, unsigned char* hash_output) {
    mbedtls_sha256_context sha256;
    mbedtls_sha256_init(&sha256);
    mbedtls_sha256_starts(&sha256, 0); // 0 = SHA256 (not SHA224)
    mbedtls_sha256_update(&sha256, (const unsigned char*)password, strlen(password));
    mbedtls_sha256_finish(&sha256, hash_output);
    mbedtls_sha256_free(&sha256);
    return true;
}

// Store hash in NVS instead of plaintext
// Compare hash during authentication
```

**Why Deferred**: 
- Current plaintext approach adequate for home/lab use
- Users can change passwords via Web UI
- Most attacks require local network access already
- Would require migration path for existing credentials

**Recommendation**: Implement if deploying in shared environments

---

#### Rate Limiting (Low Priority)
**Status**: Deferred - Document reverse proxy approach  
**Effort**: 3-4 hours  
**Benefit**: Protection against brute force attacks

**Current State**: No rate limiting on API endpoints

**Why Deferred**:
- Limited resources on ESP32 for tracking per-IP state
- Better implemented at reverse proxy level for production
- Home/lab use doesn't typically need rate limiting
- Complex implementation (track IPs, token buckets, timers)

**Recommended Approach for Production**:
```nginx
# Implement at nginx/caddy reverse proxy
limit_req_zone $binary_remote_addr zone=api:10m rate=10r/s;
limit_req zone=api burst=20 nodelay;
```

---

#### HTTPS Support (Low Priority)
**Status**: Deferred - Reverse proxy recommended  
**Effort**: 6-8 hours  
**Benefit**: Encrypted communication

**Why Deferred**:
- HTTP adequate for trusted local networks
- HTTPS overhead impacts ESP32 performance
- Certificate management complexity
- Reverse proxy is more flexible solution
- mbedtls SSL/TLS available but memory intensive

**Recommended Approach**:
```nginx
# Use nginx/caddy with Let's Encrypt
server {
    listen 443 ssl http2;
    ssl_certificate /etc/letsencrypt/live/audio.example.com/fullchain.pem;
    ssl_certificate_key /etc/letsencrypt/live/audio.example.com/privkey.pem;
    
    location / {
        proxy_pass http://192.168.1.100;  # ESP32 device
    }
}
```

**If Direct HTTPS Needed**:
- Generate self-signed certificate
- Store in SPIFFS partition
- Use mbedtls for SSL/TLS
- Monitor heap usage (SSL is memory-intensive)
- Expect ~30-40% CPU increase

---

### Architecture Improvements

#### Handle-Based Pattern (Low Priority)
**Status**: Deferred - Too invasive  
**Effort**: 16-20 hours  
**Benefit**: Better testability, multiple instances

**Current State**: Static variables for module state

**Why Deferred**:
- Current design is functional and stable
- No need for multiple instances
- Embedded systems commonly use static state
- Would require touching all modules
- Testing is currently manual (hardware-dependent)

**Example Pattern**:
```cpp
// Instead of static variables
typedef struct {
    int32_t* ring_buffer;
    size_t buffer_size;
    SemaphoreHandle_t mutex;
    // ... all state
} buffer_manager_t;

buffer_manager_t* buffer_manager_create(size_t size);
void buffer_manager_destroy(buffer_manager_t* mgr);
```

**When to Reconsider**:
- Need multiple audio streams
- Building automated test suite
- Planning library reuse

---

#### Audio Pipeline Abstraction (Low Priority)
**Status**: Deferred - Over-engineering  
**Effort**: 8-12 hours  
**Benefit**: Cleaner architecture

**Why Deferred**:
- YAGNI principle (You Aren't Gonna Need It)
- Current orchestration in main.cpp is clear
- No reuse case exists
- Adds unnecessary indirection
- Embedded code should be straightforward

**Current Approach Works Well**:
```cpp
// Simple task orchestration in main.cpp
i2s_handler_read() → buffer_manager_write()
buffer_manager_read() → tcp_streamer_send()
```

---

### Testing Infrastructure

#### Unit Test Framework (Low Priority)
**Status**: Deferred - Manual testing sufficient  
**Effort**: 20+ hours  
**Benefit**: Automated regression testing

**Why Deferred**:
- Hardware-in-loop testing more valuable for embedded
- Most modules depend on hardware (I2S, WiFi, NVS)
- Mocking ESP-IDF APIs is complex
- Manual testing currently adequate
- 20+ hours for meaningful coverage
- Hardware dependencies difficult to mock

**If Implemented**:
- Use Unity test framework (included in ESP-IDF)
- Mock WiFi/I2S with custom stubs
- Test validation_utils (easiest - no hardware)
- Test buffer_manager logic (medium difficulty)
- Focus on edge cases and error paths

---

### Performance Optimizations

#### TCP_CORK for Send Batching (Rejected)
**Status**: Rejected - May harm latency  
**Effort**: 1 hour  
**Benefit**: Reduced packet overhead

**Why Rejected**:
- Audio streaming prioritizes latency over throughput
- TCP_CORK delays transmission (Nagle's algorithm)
- Current 16KB chunks already batch efficiently
- May not be supported in ESP-IDF lwIP
- Real-time audio needs immediate transmission

**Better Alternative**:
```cpp
// Instead, optimize socket buffer sizes
int sndbuf = 16384;
setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));

// And ensure TCP_NODELAY is enabled
int nodelay = 1;
setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));
```

---

## Potential Future Features

### Web UI Enhancements

- **WebSocket for Real-Time Updates**: Replace HTTP polling with WebSocket for monitoring page
  - Effort: 6-8 hours
  - Benefit: Lower latency, reduced bandwidth
  
- **Audio Waveform Visualization**: Show real-time audio waveform in browser
  - Effort: 8-12 hours
  - Benefit: Visual feedback, debugging aid

- **Configuration Profiles**: Save/load multiple configuration presets
  - Effort: 4-6 hours
  - Benefit: Quick switching between environments

### Audio Features

- **Multiple Sample Rate Support**: Dynamic sample rate switching
  - Effort: 4-6 hours
  - Benefit: Flexibility for different use cases
  
- **Audio Filters**: Built-in high-pass/low-pass filters
  - Effort: 12-16 hours
  - Benefit: Better audio quality, noise reduction

- **Multi-Channel Support**: Stereo or multi-mic arrays
  - Effort: 16-24 hours
  - Benefit: Spatial audio, beamforming

### Connectivity

- **Bluetooth A2DP**: Stream over Bluetooth
  - Effort: 20-30 hours
  - Benefit: Wireless connectivity without WiFi
  
- **UDP Streaming**: Alternative to TCP
  - Effort: 4-6 hours
  - Benefit: Lower latency (but less reliable)

- **RTSP/RTP Support**: Standard streaming protocols
  - Effort: 30-40 hours
  - Benefit: Compatibility with standard tools

### System Features

- **SD Card Logging**: Save audio to SD card
  - Effort: 8-12 hours
  - Benefit: Local recording capability
  
- **Battery Monitoring**: Support for battery-powered operation
  - Effort: 6-8 hours
  - Benefit: Portable deployment

- **Sleep Modes**: Power saving when idle
  - Effort: 8-12 hours
  - Benefit: Reduced power consumption

## Implementation Priority Guide

### Priority 0 (Immediate)
None - All critical items completed

### Priority 1 (Next Release)
- Password hashing (if shared environment)
- WebSocket for monitoring

### Priority 2 (Future Releases)
- Configuration profiles
- Socket buffer optimization
- Dynamic sample rate

### Priority 3 (Nice to Have)
- Audio visualization
- UDP streaming
- SD card logging

### Priority 4 (Long Term)
- Bluetooth support
- RTSP/RTP protocols
- Multi-channel audio

## Re-evaluation Schedule

Review this document:
- **After 6 months**: Re-assess deferred items
- **After 1 year**: Consider major features (Bluetooth, RTSP)
- **On request**: If user needs specific feature

## Contributing

If you're interested in implementing any deferred features:

1. Open GitHub issue referencing this document
2. Discuss approach and architecture
3. Submit PR with implementation
4. Update this document with status

---

**Last Updated**: 2025-10-09  
**Next Review**: 2026-04-09 (6 months)
