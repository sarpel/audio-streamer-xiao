# TODO List - Post-Cleanup Actions

## 🔴 CRITICAL - Do Before Production

### 1. Remove Hardcoded WiFi Credentials ⚠️
**Priority:** HIGHEST  
**Effort:** 10 minutes  
**Risk:** SECURITY BREACH

**Current State:**
```cpp
// src/config.h lines 9-14
#define WIFI_SSID "Sarpel_2G"      // ❌ EXPOSED
#define WIFI_PASSWORD "penguen1988" // ❌ EXPOSED
```

**Action Required:**
```cpp
// Option A: Use empty defaults and force captive portal
#define WIFI_SSID ""
#define WIFI_PASSWORD ""

// Option B: Use environment variables (recommended)
#ifndef WIFI_SSID
#define WIFI_SSID ""  // Must be set via captive portal
#endif
```

**Impact if not fixed:**
- WiFi credentials visible in source code
- Security vulnerability if code is shared
- Compliance issues

---

### 2. Test I2S Timeout Recovery
**Priority:** HIGH  
**Effort:** 30 minutes  
**Risk:** DEVICE HANG

**Test Procedure:**
1. Start audio streaming
2. Physically disconnect INMP441 microphone
3. Verify system logs timeout (not hang)
4. Reconnect microphone
5. Verify streaming resumes

**Expected Behavior:**
```
E (12345) I2S_HANDLER: I2S read failed: ESP_ERR_TIMEOUT
E (12346) MAIN: I2S read failed (consecutive: 1)
```

**Pass Criteria:**
- [ ] No infinite hang
- [ ] Proper error logging
- [ ] Automatic recovery after reconnection

---

### 3. Validate Memory Leak Fix
**Priority:** HIGH  
**Effort:** 1 hour  
**Risk:** MEMORY EXHAUSTION

**Test Procedure:**
```cpp
// Monitor heap in watchdog task:
ESP_LOGI(TAG, "Free heap: %lu", esp_get_free_heap_size());
ESP_LOGI(TAG, "Min free heap: %lu", esp_get_minimum_free_heap_size());
```

**Test Scenario:**
1. Start device normally
2. Force TCP disconnect (unplug server)
3. Let system reconnect 50+ times
4. Monitor heap usage every 10 reconnects
5. Heap should remain stable

**Pass Criteria:**
- [ ] Heap usage doesn't decrease over time
- [ ] Min free heap stays above 50KB
- [ ] No allocation failures in logs

---

## 🟡 HIGH PRIORITY - Do This Week

### 4. Enable Strict Compiler Warnings
**Priority:** MEDIUM  
**Effort:** 2-4 hours  
**Risk:** HIDDEN BUGS

**Action:**
```ini
# platformio.ini
build_flags =
    -Wall
    -Wextra
    -Werror    # ← UNCOMMENT THIS
```

**Expected:**
- Compiler will fail on ANY warning
- May reveal 5-10 new issues to fix
- Ensures code quality

**Tasks:**
- [ ] Uncomment `-Werror` flag
- [ ] Run build and note all warnings
- [ ] Fix each warning systematically
- [ ] Verify clean build

---

### 5. Stress Test Buffer Manager
**Priority:** MEDIUM  
**Effort:** 1 hour  
**Risk:** DEADLOCK

**Test Code:**
```cpp
// Add to watchdog task:
uint32_t mutex_timeouts = buffer_manager_get_mutex_timeouts();
if (mutex_timeouts > 0) {
    ESP_LOGW(TAG, "⚠️ Buffer mutex timeouts: %lu", mutex_timeouts);
}
if (mutex_timeouts > 5) {
    ESP_LOGE(TAG, "🔴 Critical mutex issues!");
    esp_restart();
}
```

**Test Scenarios:**
1. Max buffer fill (100% full)
2. Rapid read/write cycles
3. Network congestion (slow TCP)
4. I2S underruns

**Pass Criteria:**
- [ ] Zero mutex timeouts under normal load
- [ ] Graceful recovery if timeouts occur
- [ ] No system hangs

---

### 6. Verify Stack Usage
**Priority:** MEDIUM  
**Effort:** 30 minutes  
**Risk:** STACK OVERFLOW

**Monitoring Code:**
```cpp
// Already in watchdog task:
UBaseType_t stack_remaining = uxTaskGetStackHighWaterMark(task_handle);
if (stack_remaining < MIN_STACK_WATERMARK) {
    ESP_LOGW(TAG, "⚠️ Low stack: %u bytes", stack_remaining);
}
```

**Action:**
- [ ] Run for 1 hour
- [ ] Check minimum stack remaining for each task
- [ ] Ensure > 512 bytes margin
- [ ] Adjust stack sizes if needed

---

## 🟢 MEDIUM PRIORITY - Do This Month

### 7. Reduce Log Verbosity
**Priority:** LOW  
**Effort:** 1 hour  
**Risk:** NONE

**Current Issue:**
Many `ESP_LOGI()` calls in production code create overhead.

**Action:**
```cpp
// Convert non-critical logs:
ESP_LOGI(TAG, "Buffer usage: %d%%", usage);  // ❌ Too verbose
// TO:
ESP_LOGD(TAG, "Buffer usage: %d%%", usage);  // ✅ Debug only
```

**Files to Review:**
- `src/modules/buffer_manager.cpp`
- `src/modules/tcp_streamer.cpp`
- `src/modules/i2s_handler.cpp`
- `src/main.cpp`

---

### 8. Add Unit Tests
**Priority:** LOW  
**Effort:** 8 hours  
**Risk:** REGRESSION

**Test Coverage Needed:**
```cpp
// buffer_manager_test.cpp
test_buffer_write_read()
test_buffer_overflow()
test_buffer_underflow()
test_buffer_mutex_timeout()

// tcp_streamer_test.cpp
test_connection_failure()
test_reconnection()
test_memory_leak()

// config_manager_test.cpp
test_save_load()
test_default_values()
test_invalid_data()
```

**Framework:** Unity or ESP-IDF component test

---

### 9. Performance Profiling
**Priority:** LOW  
**Effort:** 2 hours  
**Risk:** NONE

**Metrics to Collect:**
- [ ] Audio latency (mic → TCP)
- [ ] CPU usage per task
- [ ] Memory fragmentation
- [ ] I2S timing accuracy
- [ ] Network throughput

**Tools:**
- ESP-IDF built-in profiler
- Custom timing measurements
- Task stats API

---

### 10. Documentation Updates
**Priority:** LOW  
**Effort:** 4 hours  
**Risk:** NONE

**Documents Needed:**
- [ ] API documentation (Doxygen)
- [ ] Architecture diagram
- [ ] Setup guide
- [ ] Troubleshooting guide
- [ ] Contributing guidelines

---

## 📊 Progress Tracker

| Task | Priority | Status | Owner | Due Date |
|------|----------|--------|-------|----------|
| Remove WiFi Credentials | 🔴 CRITICAL | ⚠️ PENDING | USER | ASAP |
| Test I2S Timeout | 🔴 CRITICAL | ⚠️ PENDING | USER | This Week |
| Validate Memory Leak | 🔴 CRITICAL | ⚠️ PENDING | USER | This Week |
| Enable -Werror | 🟡 HIGH | ⚠️ PENDING | USER | This Week |
| Stress Test Buffer | 🟡 HIGH | ⚠️ PENDING | USER | This Week |
| Verify Stack Usage | 🟡 HIGH | ⚠️ PENDING | USER | This Week |
| Reduce Log Verbosity | 🟢 MEDIUM | ⚠️ PENDING | USER | This Month |
| Add Unit Tests | 🟢 MEDIUM | ⚠️ PENDING | USER | This Month |
| Performance Profiling | 🟢 MEDIUM | ⚠️ PENDING | USER | This Month |
| Update Documentation | 🟢 MEDIUM | ⚠️ PENDING | USER | This Month |

---

## 🎯 Definition of Done

### For Critical Tasks (1-3)
- ✅ Code changes committed
- ✅ Tests passed on hardware
- ✅ No errors in serial output
- ✅ Peer review completed (if applicable)
- ✅ Changes documented

### For High Priority Tasks (4-6)
- ✅ Code changes committed
- ✅ Build succeeds without warnings
- ✅ Basic functionality verified
- ✅ Updated README if needed

### For Medium Priority Tasks (7-10)
- ✅ Changes implemented
- ✅ No regression introduced
- ✅ Documentation updated

---

## 📞 Getting Help

### If You Encounter Issues:

1. **Build Errors**
   - Check ESP-IDF version (should be 5.5.1)
   - Clear build folder: `pio run -t clean`
   - Check `sdkconfig` for conflicts

2. **Runtime Crashes**
   - Enable core dumps in `sdkconfig`
   - Use `monitor_filters = esp32_exception_decoder`
   - Check stack sizes are sufficient

3. **Network Issues**
   - Verify WiFi signal strength
   - Check TCP server is reachable
   - Review firewall settings

---

**Created:** October 9, 2025  
**Last Updated:** October 9, 2025  
**Review Frequency:** Weekly until all critical items complete
