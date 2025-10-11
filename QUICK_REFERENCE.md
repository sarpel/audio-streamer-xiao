# Quick Reference - Code Changes

## 🔧 Constants Added to config.h

```cpp
// Timeout Configuration
#define I2S_READ_TIMEOUT_MS 5000       // I2S read operation timeout
#define TCP_SEND_TIMEOUT_MS 5000       // TCP send operation timeout  
#define BUFFER_WAIT_TIMEOUT_MS 2000    // Max wait for buffer data
#define BUFFER_WAIT_DELAY_MS 20        // Delay between buffer checks
```

## 🛡️ Compile-Time Checks Added

```cpp
#if RING_BUFFER_SIZE < 32768
#error "RING_BUFFER_SIZE too small, must be at least 32KB"
#endif

#if SAMPLE_RATE == 0
#error "SAMPLE_RATE must be greater than 0"
#endif

#if I2S_READER_STACK_SIZE < 2048
#error "I2S_READER_STACK_SIZE too small, minimum 2048 bytes"
#endif

#if TCP_SENDER_STACK_SIZE < 2048
#error "TCP_SENDER_STACK_SIZE too small, minimum 2048 bytes"
#endif
```

## 📊 New API Functions

### buffer_manager.h
```cpp
// Get mutex timeout statistics
uint32_t buffer_manager_get_mutex_timeouts(void);
```

## 🐛 Critical Fixes Applied

### 1. I2S Infinite Timeout → Bounded Timeout
**Location:** `src/modules/i2s_handler.cpp:107`
```cpp
// OLD: portMAX_DELAY (infinite)
// NEW: pdMS_TO_TICKS(5000) (5 seconds)
```

### 2. TCP Packing Buffer Memory Leak
**Location:** `src/modules/tcp_streamer.cpp:180`
```cpp
// Added check before malloc:
if (packing_buffer == NULL) {
    packing_buffer = malloc(size);
}
```

### 3. Mutex Timeout Error Tracking
**Location:** `src/modules/buffer_manager.cpp:68`
```cpp
// Added counter and recovery:
mutex_timeout_count++;
if (mutex_timeout_count >= MAX_MUTEX_TIMEOUTS) {
    ESP_LOGE(TAG, "Too many timeouts!");
}
```

### 4. Stack Buffer Overflow Protection
**Location:** `src/main.cpp:145`
```cpp
static_assert(sizeof(send_buffer) <= (TCP_SENDER_STACK_SIZE / 2), 
              "send_buffer too large for task stack!");
```

## ⚙️ PlatformIO Changes

### Removed Duplicates
```ini
; Removed duplicate monitor_filters line
; Removed commented board_build.variant line
```

### Added Build Flags
```ini
build_flags =
    -DCORE_DEBUG_LEVEL=3
    -Wall
    -Wextra
```

### Enhanced Monitor Filters
```ini
monitor_filters = 
    esp32_exception_decoder
    colorize
```

## 📝 Usage Examples

### Check Buffer Health
```cpp
uint32_t timeouts = buffer_manager_get_mutex_timeouts();
if (timeouts > 0) {
    ESP_LOGW(TAG, "Buffer mutex timeouts detected: %lu", timeouts);
}
```

### Monitor in Watchdog Task
```cpp
// In watchdog_task():
uint32_t mutex_errors = buffer_manager_get_mutex_timeouts();
if (mutex_errors > MAX_MUTEX_TIMEOUTS) {
    ESP_LOGE(TAG, "Critical: System requires restart");
    esp_restart();
}
```

## 🎯 Testing Checklist

- [ ] Compile with new checks (should pass)
- [ ] Test I2S timeout recovery
- [ ] Test TCP reconnection memory stability  
- [ ] Monitor buffer mutex timeouts under load
- [ ] Verify stack usage doesn't exceed limits
- [ ] Test with WiFi disconnection scenarios

## ⚠️ Important Notes

1. **Credentials Still Hardcoded**  
   Remove from `config.h` before production deployment

2. **Compile-Time Checks Active**  
   Changing buffer sizes may trigger errors - this is intentional

3. **New Statistics Available**  
   Use `buffer_manager_get_mutex_timeouts()` for monitoring

4. **Timeouts Now Bounded**  
   System won't hang indefinitely on hardware failures

---

**Last Updated:** October 9, 2025  
**Version:** 1.0 (Post-Cleanup)
