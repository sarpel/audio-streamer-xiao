# PSRAM Initialization Fix

## Problem
Firmware crashed on boot with error:
```
E (262) quad_psram: PSRAM ID read error: 0x00ffffff, PSRAM chip not found or not supported
E cpu_start: Failed to init external RAM!
abort() was called at PC 0x40376050 on core 0
```

**Root Cause**: The Seeed XIAO ESP32-S3 basic variant does NOT have PSRAM. Only the "Sense" variant includes 8MB PSRAM. The firmware was configured for PSRAM, causing a fatal initialization failure.

## Solution Applied

### 1. Disabled SPIRAM in Configuration Files

**Files Modified:**
- `sdkconfig` (line 1194)
- `sdkconfig.xiao_esp32s3` (line 1180)

**Change:**
```diff
- CONFIG_SPIRAM=y
- # SPI RAM config
- CONFIG_SPIRAM_MODE_QUAD=y
- CONFIG_SPIRAM_TYPE_AUTO=y
- [... 25+ SPIRAM configuration lines ...]
+ # CONFIG_SPIRAM is not set
+ # end of SPI RAM config
```

### 2. Adjusted Ring Buffer for Internal SRAM

**File Modified:** `src/config.h` (line 50)

**Change:**
```diff
- #define RING_BUFFER_SIZE (1024 * 1024)  // 1 MB in PSRAM
+ #define RING_BUFFER_SIZE (128 * 1024)   // 128 KB in internal SRAM (ESP32-S3 has 512KB total)
```

## Impact Analysis

### Memory Usage
- **Previous**: 1MB ring buffer in external PSRAM (not available)
- **Current**: 128KB ring buffer in internal SRAM

### Audio Buffering Capacity
- **Buffer Duration**: 128KB ÷ (16000 samples/sec × 2 bytes/sample) = **~4 seconds** of audio
- **Previous (1MB)**: ~32 seconds of audio
- **Conclusion**: 4 seconds is sufficient for network buffering and handling temporary interruptions

### ESP32-S3 SRAM Availability
- **Total Internal SRAM**: 512KB
- **Ring Buffer**: 128KB (25%)
- **Remaining**: ~384KB for:
  - WiFi stack (~50-80KB)
  - FreeRTOS tasks (~24KB total)
  - TCP buffers
  - System overhead

## Rebuild Instructions

```bash
# Navigate to project directory
cd D:\MCP\ClaudeCode\seeed-xiao\audio-streamer-xiao

# Clean previous build
idf.py fullclean

# Rebuild with new configuration
idf.py build

# Flash to device
idf.py flash

# Monitor output
idf.py monitor
```

## Expected Boot Sequence (After Fix)

```
I (xxx) cpu_start: Starting scheduler on APP CPU.
I (xxx) main_task: Calling app_main()
I (xxx) MAIN: ====================================
I (xxx) MAIN: Audio Streamer for XIAO ESP32-S3
I (xxx) MAIN: ====================================
I (xxx) MAIN: Initializing network...
[... normal boot continues without PSRAM error ...]
```

## Hardware Compatibility

### Seeed XIAO ESP32-S3 Variants

| Variant | PSRAM | Configuration |
|---------|-------|---------------|
| Basic   | None  | Use this fix (CONFIG_SPIRAM disabled) |
| Sense   | 8MB   | Can enable SPIRAM for 8MB PSRAM |

## Technical Notes

1. **PSRAM Detection**: ESP-IDF attempts PSRAM initialization during bootloader phase. If PSRAM is configured but not present, boot fails immediately.

2. **Internal SRAM**: ESP32-S3 has 512KB internal SRAM shared between:
   - System heap
   - FreeRTOS task stacks
   - WiFi/Bluetooth stacks
   - User allocations

3. **Buffer Sizing**: 128KB ring buffer balances:
   - Sufficient audio buffering (4 seconds)
   - Leaves adequate SRAM for other system needs
   - Can be adjusted based on testing

## Files Changed

1. `sdkconfig` - Disabled CONFIG_SPIRAM
2. `sdkconfig.xiao_esp32s3` - Disabled CONFIG_SPIRAM
3. `src/config.h` - Reduced RING_BUFFER_SIZE from 1MB to 128KB

## Date
2025-10-07
