# PR #5 Action Plan - Captive Portal Implementation

**Pull Request:** feat: Implement captive portal for WiFi configuration on connection failure  
**Analysis Date:** October 16, 2025 (Updated after codebase verification)  
**Branch:** asm  
**Status:** Active PR - Verified against current codebase

---

## ✅ RESOLVED/IMPLEMENTED Issues (Already in codebase)

### ✅ 1. i2s_read_16 Buffer Overflow Protection - RESOLVED
**File:** `src/modules/i2s_handler.cpp` (lines 158-162)  
**Status:** ✅ **IMPLEMENTED** - Code already has chunking logic  
**Implementation:**
```c
size_t chunk_samples = samples_remaining > I2S_READ_SAMPLES ? I2S_READ_SAMPLES : samples_remaining;
```
The function now processes large requests in chunks, preventing buffer overflow. No action needed.

---

### ✅ 2. strncpy Null Termination - RESOLVED
**File:** `src/modules/config_schema.cpp` (line 652)  
**Status:** ✅ **IMPLEMENTED**  
**Implementation:**
```c
buffer[buffer_size - 1] = '\0';
```
Added at end of switch statement to ensure all strncpy operations are null-terminated. No action needed.

---

### ✅ 3. platformio.ini Typo - RESOLVED
**Status:** ✅ Fixed in commit 38c462f  
No action needed.

---

### ✅ 4. SPIRAM Flag - RESOLVED
**Status:** ✅ Added in commit 38c462f  
No action needed.

---

## 🔴 CRITICAL - System Stability Issues (Still Need Fixing)

### 1. Fix i2s_read_16 Thread Safety 🔒

**Priority:** CRITICAL  
**Status:** ❌ **STILL UNRESOLVED**  
**File:** `src/modules/i2s_handler.cpp` (line 153)  
**Reported By:** Copilot, ChatGPT, CodeRabbit

**Issue:**
- Static buffer `static int32_t tmp[I2S_READ_SAMPLES]` is shared across all calls
- Non-reentrant function causes data races when called concurrently
- Multiple tasks accessing this function will corrupt each other's data

**Current Code:**
```c
static int32_t tmp[I2S_READ_SAMPLES];  // ← SHARED STATIC BUFFER
size_t total_samples_read = 0;
size_t samples_remaining = samples;
```

**Action Required (Option 1 - Preferred):**
```c
// Change function signature:
size_t i2s_read_16(int16_t *out, int32_t *tmp_buffer, size_t samples)

// Caller allocates buffer:
int32_t temp_buffer[I2S_READ_SAMPLES];
i2s_read_16(output, temp_buffer, sample_count);
```

**Action Required (Option 2 - Slower):**
```c
// Add mutex protection
static SemaphoreHandle_t i2s_read_mutex = NULL;

// In i2s_handler_init():
i2s_read_mutex = xSemaphoreCreateMutex();

// In i2s_read_16():
if (xSemaphoreTake(i2s_read_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
    ESP_LOGE(TAG, "Failed to acquire mutex");
    return 0;
}
// ... existing code ...
xSemaphoreGive(i2s_read_mutex);
```

**Impact:** Data corruption, unpredictable audio glitches, race conditions

---

### 2. Fix Authentication System 🚨

**Priority:** CRITICAL  
**Status:** ❌ **STILL UNRESOLVED - PASSWORD IS MASKED**  
**File:** `src/modules/web_server_v2.cpp` (lines 79-83)  
**Reported By:** CodeRabbit

**Current Code:**
```c
if (!config_manager_v2_get_field(CONFIG_FIELD_AUTH_USERNAME, username, sizeof(username)) ||
    !config_manager_v2_get_field(CONFIG_FIELD_AUTH_PASSWORD, password, sizeof(password)))
{
    ESP_LOGW(TAG, "Failed to get auth config");
    return false;
}
```

**Issue:**
- `config_manager_v2_get_field()` returns masked password ("********") for security fields
- Basic Auth comparison at line 137 always fails since real password never matches mask
- **ALL REST API endpoints requiring auth are inaccessible**

**Action Required:**
```c
// Option 1: Add unmasked retrieval function to config_manager_v2
bool config_manager_v2_get_field_raw(config_field_id_t field_id, 
                                     char *buffer, size_t buffer_size);

// Option 2: Direct access to unified_config (if available)
unified_config_t config;
config_manager_v2_get_config(&config);
strncpy(password, config.auth_password, sizeof(password));
password[sizeof(password) - 1] = '\0';
```

**Impact:** Complete authentication failure, API unusable, captive portal broken

---

### 3. Fix Type for input_gain_db 📊

**Priority:** CRITICAL  
**Status:** ❌ **STILL UNRESOLVED**  
**File:** `src/modules/config_manager.h` (line 42)  
**Reported By:** CodeRabbit

**Current Code:**
```c
uint8_t input_gain_db; // -40 to +40 dB  ← WRONG TYPE
```

**Issue:**
- Field declared as `uint8_t` (0-255 range)
- Comment says range is -40 to +40 dB
- Cannot store negative values

**Action Required:**
```c
// Change from:
uint8_t input_gain_db; // -40 to +40 dB

// Change to:
int8_t input_gain_db; // -40 to +40 dB
```

**Impact:** Invalid gain values, audio processing errors

---

## 🟠 HIGH PRIORITY - Data Loss / Functionality

### 4. POST /api/config/network Doesn't Save Data 💾

**Priority:** HIGH  
**Status:** ❌ **STILL UNRESOLVED - PLACEHOLDER IMPLEMENTATION**  
**File:** `src/modules/web_server_v2.cpp` (lines 438-470)  
**Reported By:** CodeRabbit

**Current Code:**
```c
cJSON *root = cJSON_Parse(buf);
if (!root) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
    return ESP_FAIL;
}

cJSON *response = cJSON_CreateObject();
cJSON_AddStringToObject(response, "status", "success");
cJSON_AddStringToObject(response, "message", "Network configuration updated. Restart required to apply changes.");
cJSON_AddBoolToObject(response, "restart_required", true);

cJSON_Delete(root);  // ← Deletes data without saving!
```

**Issue:**
- Function parses JSON successfully
- Immediately deletes parsed data without saving
- No calls to `config_manager_v2_set_field()`
- Returns "success" message but configuration is lost

**Action Required:**
```c
// Add field setting logic (similar to api_post_wifi_handler):
bool changed = false;
config_validation_result_t validation;

cJSON *ssid = cJSON_GetObjectItem(root, "ssid");
if (ssid && cJSON_IsString(ssid)) {
    changed |= config_manager_v2_set_field(CONFIG_FIELD_WIFI_SSID, 
                                           ssid->valuestring, &validation);
}

// ... set other fields (tcp_ip, udp_ip, ports, etc.) ...

if (changed && !config_manager_v2_save()) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, 
                       "Failed to save network configuration");
    cJSON_Delete(root);
    return ESP_FAIL;
}
```

**Impact:** Complete data loss for network configuration, user frustration

---

### 5. Legacy Config Migration Broken 🔄

**Priority:** HIGH  
**Status:** ❌ **STILL UNRESOLVED - NULL POINTER PASSED**  
**File:** `src/modules/config_manager_v2.cpp` (lines 73, 114)  
**Reported By:** CodeRabbit

**Current Code:**
```c
if (config_schema_convert_legacy(NULL, &legacy_config)) {  // ← NULL passed!
```

**Issue:**
- Passes `NULL` as first parameter to `config_schema_convert_legacy()`
- Function returns false immediately, no migration occurs
- Users lose all existing configuration on upgrade
- Found in two locations (lines 73 and 114)

**Action Required:**
```c
// Either pass valid pointer:
legacy_config_t legacy_cfg;
if (config_schema_convert_legacy(&legacy_cfg, &current_config)) {
    // Migration succeeded
    has_unsaved_changes = true;
}

// OR: Modify config_schema_convert_legacy to accept NULL and 
// internally read from NVS
```

**Impact:** Configuration loss on firmware upgrade

---

### 6. Add Missing esp_restart() Calls 🔄

**Priority:** HIGH  
**Status:** ❌ **STILL UNRESOLVED - NO RESTART ON INIT FAILURES**  
**Files:**
- `src/modules/i2s_handler.cpp` (lines 22, 73, 83)
- `src/modules/tcp_streamer.cpp` (line 116)
- `src/modules/udp_streamer.cpp` 
- `src/modules/performance_monitor.cpp` 
- `src/modules/config_manager_v2.cpp` 

**Reported By:** CodeRabbit (multiple comments)

**Issue:**
- Init failures return `false` without restarting
- Per coding guidelines: "modül init çökerse logla ve esp_restart() yap"
- System left in undefined state after init failure

**Action Required:**
```c
// Example pattern for all init failures:
if (init_operation_failed()) {
    ESP_LOGE(TAG, "Module init failed, restarting...");
    esp_restart();
    return false; // Unreachable, but kept for clarity
}
```

**Affected Modules:**
1. I2S Handler - channel creation (line 22), mode init (line 73), enable (line 83)
2. TCP Streamer - buffer allocation (line 116), connection failures
3. UDP Streamer - buffer allocation, socket creation
4. Performance Monitor - mutex creation, task creation
5. Config Manager V2 - NVS initialization

**Impact:** Undefined system behavior, partial initialization, hard to debug issues

---

### 7. Fix SSID Null-Terminator Check ⚠️

**Priority:** HIGH  
**Status:** ❌ **STILL UNRESOLVED - UNSAFE STRING OPERATIONS**  
**File:** `src/modules/captive_portal.cpp` (lines 213-216)  
**Reported By:** CodeRabbit

**Current Code:**
```c
if (strlen(config.wifi_ssid) == 0 ||
    strcmp(config.wifi_ssid, "CHANGE_ME") == 0 ||
    strcmp(config.wifi_ssid, "your_wifi") == 0)
{
    return false;
}
```

**Issue:**
- Uses `strlen()` and `strcmp()` on `config.wifi_ssid` without null-terminator guarantee
- Undefined behavior if buffer not properly terminated
- Can read beyond buffer bounds

**Action Required:**
```c
// Replace unsafe string operations:
const char *term = (const char *)memchr(config.wifi_ssid, '\0', sizeof(config.wifi_ssid));
if (!term) {
    return false; // NUL not found: invalid
}
size_t n = (size_t)(term - config.wifi_ssid);
if (n == 0 ||
    (n == sizeof("CHANGE_ME") - 1 && memcmp(config.wifi_ssid, "CHANGE_ME", n) == 0) ||
    (n == sizeof("your_wifi") - 1 && memcmp(config.wifi_ssid, "your_wifi", n) == 0)) {
    return false;
}
```

**Impact:** Potential crashes, undefined behavior, memory access violations

---

### 8. Check config_manager_v2_save() Return Values ✅

**Priority:** HIGH  
**Status:** ⚠️ **PARTIALLY RESOLVED** - Some instances still unchecked  
**File:** `src/modules/web_server_v2.cpp` (lines 576, 838)  
**Reported By:** CodeRabbit

**Verified Status:**
- ✅ Line 332: `api_post_wifi_handler()` - **CHECKS return value**
- ✅ Line 658: `api_post_all_config_handler()` - **CHECKS return value**
- ✅ Line 899: `api_post_save_config_handler()` - **CHECKS return value**
- ❌ Line 576: `api_post_audio_handler()` - **DOES NOT CHECK**
- ❌ Line 838: `api_post_restart_handler()` - **DOES NOT CHECK** (lower priority)

**Action Required:**
```c
// Line 576 in api_post_audio_handler():
if (changed) {
    if (!config_manager_v2_save()) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, 
                           "Failed to save audio configuration");
        return ESP_FAIL;
    }
    captive_portal_mark_config_updated();
}
```

**Impact:** Silent data loss in audio configuration, user confusion

---

## 🟡 MEDIUM PRIORITY - Code Quality

### 9. Fix TCP Buffer Size Calculation 📏

**Priority:** MEDIUM  
**Status:** ❌ **STILL UNRESOLVED - MAGIC NUMBER**  
**File:** `src/modules/tcp_streamer.cpp` (line 111)  
**Reported By:** CodeRabbit

**Current Code:**
```c
packing_buffer_size = 16384 * 2; // Max samples × bytes per sample
```

**Issue:**
- Hardcoded magic number: `16384 * 2`
- Should derive from configuration constants
- Makes maintenance difficult and error-prone
- Comment says "Max samples × bytes per sample" but calculation doesn't use constants

**Action Required:**
```c
// Replace:
packing_buffer_size = 16384 * 2;

// With:
packing_buffer_size = TCP_SEND_SAMPLES * BYTES_PER_SAMPLE;
// Or with safety margin:
packing_buffer_size = (TCP_SEND_SAMPLES * BYTES_PER_SAMPLE) + 1024;
```

**Impact:** Maintenance difficulty, potential buffer size mismatches

---

### 10. Fix Audio Data Rate Calculation 🎵

**Priority:** MEDIUM  
**Status:** ❌ **STILL UNRESOLVED - NAME/VALUE MISMATCH**  
**File:** `src/modules/performance_monitor.cpp` (line 201)  
**Reported By:** CodeRabbit

**Current Code:**
```c
metrics.audio_data_rate_bps = SAMPLE_RATE * CHANNELS * (BITS_PER_SAMPLE / 8);
```

**Issue:**
- Field name: `audio_data_rate_bps` (implies **bits** per second)
- Calculation: `SAMPLE_RATE * CHANNELS * (BITS_PER_SAMPLE / 8)` (produces **bytes** per second)
- Mismatch between name and value

**Action Required (Option 1 - Preferred):**
```c
// Fix calculation to produce bits/sec:
metrics.audio_data_rate_bps = SAMPLE_RATE * CHANNELS * BITS_PER_SAMPLE;
```

**Action Required (Option 2):**
```c
// Rename field to match current calculation:
// In performance_monitor.h, change:
// uint32_t audio_data_rate_bps; → uint32_t audio_data_rate_bytes_per_sec;
```

**Impact:** Misleading metrics, incorrect monitoring, API consumers get wrong values

---

### 11. Add Missing Include 📦

**Priority:** MEDIUM  
**Status:** ❌ **STILL UNRESOLVED - MISSING HEADER**  
**File:** `src/modules/performance_monitor.cpp` (lines 1-14)  
**Reported By:** CodeRabbit

**Current Code:**
```c
#include "../config.h"
#include "performance_monitor.h"
#include "tcp_streamer.h"
#include "udp_streamer.h"
#include "buffer_manager.h"
#include "network_manager.h"
#include "config_manager.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
// ← Missing esp_heap_caps.h
```

**Issue:**
- Uses `heap_caps_get_largest_free_block()` at line 183
- Missing `#include "esp_heap_caps.h"`
- Currently compiling due to transitive includes, but fragile

**Action Required:**
```c
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"  // ← Add this line
#include "esp_wifi.h"
```

**Impact:** Potential compilation errors if transitive includes change

---

### 12. Fix Duplicate Macro Definitions 🔁

**Priority:** MEDIUM  
**Status:** ❌ **STILL UNRESOLVED - DUPLICATES IN TWO FILES**  
**Files:**
- `src/config.h` (line 212): `#define MAX_HISTORY_ENTRIES 720`
- `src/modules/performance_monitor.h` (line 58): `#define MAX_HISTORY_ENTRIES 720`

**Reported By:** CodeRabbit

**Issue:**
- `HISTORY_INTERVAL_MS` and `MAX_HISTORY_ENTRIES` defined in both files
- Risk of redefinition warnings and value drift
- Violates DRY principle

**Action Required (Option 1 - Preferred):**
```c
// In performance_monitor.h, remove local definitions:
// Remove lines:
// #define MAX_HISTORY_ENTRIES 720
// #define HISTORY_INTERVAL_MS 10000

// Already includes config.h via other headers, so definitions available
```

**Action Required (Option 2):**
```c
// Use include guards:
#ifndef MAX_HISTORY_ENTRIES
#define MAX_HISTORY_ENTRIES 720
#endif

#ifndef HISTORY_INTERVAL_MS
#define HISTORY_INTERVAL_MS 10000
#endif
```

**Impact:** Build warnings, potential configuration inconsistencies

---

### 13. Fix JSON Import Placeholder 📥

**Priority:** MEDIUM  
**Status:** ❌ **STILL UNRESOLVED - RETURNS TRUE FOR UNIMPLEMENTED FEATURE**  
**File:** `src/modules/config_manager.cpp` (lines 861-878)  
**Reported By:** CodeRabbit

**Current Code:**
```c
bool config_manager_import_json(const char* json_input, bool overwrite)
{
    // ... validation code ...

    // This is a placeholder implementation
    // A full implementation would parse JSON and update the configuration structures
    ESP_LOGW(TAG, "JSON import not fully implemented - placeholder");

    return true;  // ← WRONG: Returns success for unimplemented feature
}
```

**Issue:**
- Function logs "not fully implemented - placeholder"
- Returns `true` (success) despite not doing anything
- Misleads calling code and users

**Action Required:**
```c
// Change from:
ESP_LOGW(TAG, "JSON import not fully implemented - placeholder");
return true;

// Change to:
ESP_LOGW(TAG, "JSON import not implemented");
return false;
```

**Impact:** Misleading return values, silent feature failures, user confusion

---

## 🔵 LOW PRIORITY - Documentation / Style

### 14. Fix bits_per_sample Documentation 📖

**Priority:** LOW  
**Status:** ❌ **STILL UNRESOLVED - MISLEADING COMMENT**  
**File:** `src/modules/config_manager.cpp` (I2S initialization)  
**Reported By:** Copilot

**Issue:**
- `bits_per_sample` set to 16 (output format after conversion)
- I2S hardware configured for 24-bit data (Philips, 32-bit slot)
- Mismatch can mislead consumers of i2s_config structure
- Comment doesn't explain the conversion pipeline

**Action Required:**
```c
i2s_config.sample_rate = SAMPLE_RATE;
// Note: bits_per_sample is the post-conversion output format (16-bit)
// while I2S hardware captures 24-bit data in 32-bit slots (Philips standard)
// Conversion from 24-bit to 16-bit happens in i2s_handler.cpp
i2s_config.bits_per_sample = BITS_PER_SAMPLE;  // Output: 16-bit
i2s_config.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
i2s_config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
```

**Impact:** Documentation clarity, prevents confusion about audio pipeline

---

### 15. Fix API Surface Contract 🏗️

**Priority:** LOW  
**Status:** ❌ **STILL UNRESOLVED - API SCOPE UNCLEAR**  
**Files:**
- `src/modules/buffer_manager.h` (lines 24-29, 46-51)
- `src/modules/tcp_streamer.h` (lines 19-24)
- `src/modules/udp_streamer.h`

**Reported By:** CodeRabbit

**Issue:**
- New 16-bit specific APIs (`write_16`, `read_16`, `send_audio_16`) expand interface
- Original contract specified exact API surface: init, write, read, usage_percent, reset, deinit
- New functions not in original specification
- Unclear if these are public API or implementation details

**Action Required:**
Decide on approach:

**Option 1 - Keep Internal (Recommended):**
```c
// Remove from public headers
// Keep as static/internal functions in .cpp files
static size_t buffer_manager_write_16_internal(const int16_t *data, size_t samples);
```

**Option 2 - Document Extension:**
```c
// Add clear documentation explaining the 16-bit API extension
/**
 * Extended API: 16-bit audio support
 * These functions were added for optimized 16-bit audio handling
 * @note Not part of original API specification
 */
size_t buffer_manager_write_16(const int16_t *data, size_t samples);
```

**Impact:** API consistency, contract compliance, maintainability

---

### 16. Remove Unrelated File 🗑️

**Priority:** LOW  
**Status:** ❌ **STILL PRESENT - UNRELATED FILE IN REPO**  
**File:** `Z.ai.ps1` (root directory)  
**Reported By:** CodeRabbit

**Issue:**
- PowerShell script for GLM-4.6 AI model execution (`$Env:GLM_API_KEY`)
- Completely unrelated to audio streamer project or captive portal feature
- Should not be in version control (personal utility script)
- Adds clutter to project root
- Accidentally included in PR #5

**Action Required:**
```bash
# Remove from git:
git rm Z.ai.ps1
git commit -m "Remove accidentally included AI script"

# Optional: Add to .gitignore if you use it locally:
echo "Z.ai.ps1" >> .gitignore
```

**Impact:** Repository cleanliness, PR focus, professional appearance

---

### 17. Fix Markdown Bare URLs 🔗

**Priority:** LOW  
**Status:** ❌ **STILL UNRESOLVED - LINTER WARNINGS**  
**File:** `CAPTIVE_PORTAL_FIX.md` (lines 148, 167, 181)  
**Reported By:** CodeRabbit (markdownlint - MD034)

**Issue:**
- Naked URLs like `http://192.168.4.1` cause linter warnings (MD034: no-bare-urls)
- Should be wrapped in angle brackets or links for proper markdown formatting
- Affects documentation quality checks in CI/CD pipeline
- Found in 3 locations in the captive portal documentation

**Action Required:**
```markdown
<!-- Replace: -->
4. Navigate to http://192.168.4.1

<!-- With (Option 1 - Angle brackets, recommended): -->
4. Navigate to <http://192.168.4.1>

<!-- Or (Option 2 - Markdown link): -->
4. Navigate to [192.168.4.1](http://192.168.4.1)
```

**Apply to lines 148, 167, and 181 in CAPTIVE_PORTAL_FIX.md**

**Impact:** Documentation linting, markdown best practices, CI checks pass

---

## ✅ RESOLVED ISSUES

### ✅ 1. Fixed platformio.ini Typo
**Status:** ✅ Resolved in commit 38c462f  
**File:** `platformio.ini` (line 22)  
**Issue:** `board_build.board_build.f_flash` → `board_build.f_flash`

### ✅ 2. Added SPIRAM Flag
**Status:** ✅ Resolved in commit 38c462f  
**File:** `platformio.ini`  
**Issue:** Added `-DCONFIG_SPIRAM_SUPPORT=0` to build_flags

### ✅ 3. Check config_manager_v2_save() in Audio Handler
**Status:** ✅ Resolved in commit 2623a55  
**File:** `src/modules/web_server_v2.cpp`  
**Issue:** One instance fixed, but multiple other instances remain

---

## 📊 SUMMARY

### Issue Count by Priority
- 🔴 **CRITICAL:** 3 issues
- 🟠 **HIGH:** 5 issues  
- 🟡 **MEDIUM:** 5 issues
- 🔵 **LOW:** 4 issues
- ✅ **RESOLVED:** 4 issues

### Total: 17 Active Issues (21 including resolved)

---

## 🎯 RECOMMENDED IMPLEMENTATION ORDER

### Phase 1: System Stability (Week 1) - **START HERE**
1. **Fix authentication system (#2)** - 🔴 CRITICAL - **BLOCKS ALL API ACCESS**
2. **Fix i2s_read_16() thread safety (#1)** - 🔴 CRITICAL - **PREVENTS CRASHES**
3. **Fix type for input_gain_db (#3)** - 🔴 CRITICAL - **DATA CORRUPTION**

### Phase 2: Data Integrity (Week 1-2) - **HIGH PRIORITY**
4. **Implement POST /api/config/network (#4)** - 🟠 HIGH - Currently placeholder
5. **Fix legacy config migration (#5)** - 🟠 HIGH - NULL pointer passing
6. **Add esp_restart() on init failures (#6)** - 🟠 HIGH - System hangs on error
7. **Fix SSID null-terminator checks (#7)** - 🟠 HIGH - Buffer overrun risk
8. **Check all config_manager_v2_save() calls (#8)** - 🟠 HIGH - Silent data loss

### Phase 3: Code Quality (Week 2-3) - **MEDIUM PRIORITY**
9. **Fix TCP buffer size calculation (#9)** - 🟡 MEDIUM - Magic numbers
10. **Fix audio data rate calculation (#10)** - 🟡 MEDIUM - Name/value mismatch
11. **Add missing include (#11)** - 🟡 MEDIUM - Fragile compilation
12. **Fix duplicate macro definitions (#12)** - 🟡 MEDIUM - Build warnings
13. **Fix JSON import placeholder (#13)** - 🟡 MEDIUM - Wrong return value

### Phase 4: Documentation & Cleanup (Week 3-4) - **LOW PRIORITY**
14. **Fix bits_per_sample documentation (#14)** - 🔵 LOW - Comment clarity
15. **Fix API surface contract (#15)** - 🔵 LOW - Scope decision
16. **Remove unrelated file (#16)** - 🔵 LOW - Z.ai.ps1
17. **Fix markdown URLs (#17)** - 🔵 LOW - Linter warnings

---

**Implementation Notes:**
- **CRITICAL issues** should be fixed first - they block testing or cause crashes
- **HIGH priority** issues cause data loss or system instability
- **MEDIUM priority** issues are technical debt that should be addressed
- **LOW priority** issues are polish/cleanup items
- Some issues have multiple instances across files (e.g., save() checks)

---

## 🔧 TESTING REQUIREMENTS

After implementing fixes, test:

### Critical Path Testing
- [ ] Basic Auth login works with correct credentials
- [ ] WiFi configuration saves and persists across reboots
- [ ] I2S audio capture with concurrent access (multi-task)
- [ ] Network configuration POST actually saves data
- [ ] Legacy config migration from v1 to v2

### Stability Testing
- [ ] All module init failures trigger restart
- [ ] No crashes under concurrent audio access
- [ ] Memory remains stable (no leaks, no corruption)
- [ ] Buffer overflow protection works

### Integration Testing
- [ ] Captive portal flow end-to-end
- [ ] OTA updates preserve configuration
- [ ] Factory reset works completely
- [ ] All REST API endpoints function correctly

---

## 📝 NOTES

- **Security issues excluded** as requested (e.g., password exposure in JSON)
- Focus on **stability, functionality, and code quality**
- Some issues have multiple instances across codebase
- **Authentication (#3) should be fixed FIRST** - it blocks all testing
- Consider creating separate issues for each item for tracking

---

## 🔗 REFERENCES

- **PR URL:** <https://github.com/sarpel/audio-streamer-xiao/pull/5>
- **Branch:** asm
- **Base Branch:** master
- **Review Comments:** Gemini, Copilot, ChatGPT, CodeRabbit
- **Coding Guidelines:** `.github/copilot-instructions.md`, `mcu.instructions.md`

---

**Last Updated:** October 16, 2025  
**Next Review:** After Phase 1 completion
