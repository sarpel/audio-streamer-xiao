# Captive Portal Fix

## Problem Description
The captive portal was not activating when WiFi connection failed. The MCU would not host an access point for entering WiFi credentials when there was no successful WiFi connection.

## Root Causes Identified

### 1. Flawed Configuration Check Logic
**File**: `src/modules/captive_portal.cpp` - `captive_portal_is_configured()`

**Original Issue**:
```cpp
return (strlen(wifi.ssid) > 0 && strcmp(wifi.ssid, WIFI_SSID) != 0) ||
       config_manager_is_first_boot() == false;
```

This logic was problematic because:
- It compared against the hardcoded `WIFI_SSID` from `config.h`
- If the stored SSID matched the hardcoded one (which it would on first boot), it would return `true` (configured)
- The OR condition with `!is_first_boot()` meant it would always return `true` after first boot, even if WiFi wasn't working

**Fix Applied**:
```cpp
bool captive_portal_is_configured(void) {
    // First boot means definitely not configured via captive portal
    if (config_manager_is_first_boot()) {
        return false;
    }
    
    wifi_config_data_t wifi;
    if (!config_manager_get_wifi(&wifi)) {
        return false;
    }
    
    // Check if SSID is set and not empty/default placeholder
    if (strlen(wifi.ssid) == 0 || 
        strcmp(wifi.ssid, "CHANGE_ME") == 0 ||
        strcmp(wifi.ssid, "your_wifi") == 0) {
        return false;
    }
    
    return true;
}
```

### 2. Early Exit Preventing Portal Launch
**File**: `src/modules/captive_portal.cpp` - `captive_portal_init()`

**Original Issue**:
```cpp
// Check if already configured
if (captive_portal_is_configured()) {
    ESP_LOGI(TAG, "Device already configured, skipping captive portal");
    return false;
}
```

This prevented the captive portal from starting even when explicitly called after WiFi failure.

**Fix Applied**: Removed this check - the caller should decide when to invoke the captive portal.

### 3. No Fallback on WiFi Connection Failure
**File**: `src/main.cpp` - `app_main()`

**Original Issue**:
```cpp
if (!network_manager_init()) {
    ESP_LOGE(TAG, "CRITICAL: WiFi init failed, rebooting in 5 seconds...");
    vTaskDelay(pdMS_TO_TICKS(5000));
    esp_restart();
}
```

The device would simply reboot on WiFi failure instead of offering captive portal for reconfiguration.

**Fix Applied**:
```cpp
if (!network_manager_init()) {
    ESP_LOGE(TAG, "WiFi connection failed, starting captive portal...");
    
    // Start captive portal for configuration
    if (captive_portal_init()) {
        ESP_LOGI(TAG, "Captive portal active. Connect to '%s' to configure.", CAPTIVE_PORTAL_SSID);

        // Initialize web server for configuration in AP mode
        if (web_server_init()) {
            ESP_LOGI(TAG, "Web configuration available at http://192.168.4.1");
        }

        // Wait for configuration (no timeout when triggered by WiFi failure)
        while (captive_portal_is_active()) {
            vTaskDelay(pdMS_TO_TICKS(1000));

            // Check if configuration was saved
            if (captive_portal_is_configured()) {
                ESP_LOGI(TAG, "Configuration received, rebooting to apply...");
                captive_portal_stop();
                web_server_deinit();
                vTaskDelay(pdMS_TO_TICKS(2000));
                esp_restart();
            }
        }
    }
    
    // If captive portal failed to start, reboot
    ESP_LOGE(TAG, "CRITICAL: Could not start captive portal, rebooting in 5 seconds...");
    vTaskDelay(pdMS_TO_TICKS(5000));
    esp_restart();
}
```

### 4. WiFi State Conflict
**File**: `src/modules/captive_portal.cpp` - `captive_portal_init()`

**Issue**: When captive portal is started after a failed WiFi connection attempt, the WiFi may still be in an inconsistent state.

**Fix Applied**:
```cpp
// Stop WiFi if it's already running from a failed connection attempt
esp_wifi_stop();
esp_wifi_deinit();

// Small delay to ensure WiFi is fully stopped
vTaskDelay(pdMS_TO_TICKS(100));
```

Also added required FreeRTOS headers for task delay functionality.

## Changes Summary

### Files Modified:
1. **`src/modules/captive_portal.cpp`**
   - Fixed `captive_portal_is_configured()` logic
   - Removed blocking check in `captive_portal_init()`
   - Added WiFi cleanup before AP mode initialization
   - Added FreeRTOS headers

2. **`src/main.cpp`**
   - Changed WiFi failure handling to trigger captive portal instead of immediate reboot
   - Added infinite wait loop for captive portal configuration (when triggered by WiFi failure)

## Testing Instructions

### Test 1: First Boot
1. Flash the firmware to a device with erased NVS
2. Device should start captive portal automatically
3. Connect to `AudioStreamer-Setup` AP
4. Navigate to http://192.168.4.1
5. Configure WiFi credentials
6. Device should reboot and connect to configured network

### Test 2: Wrong WiFi Credentials
1. Configure device with incorrect WiFi credentials via web interface
2. Reboot device
3. Device should fail to connect to WiFi
4. Device should automatically start captive portal
5. Connect to `AudioStreamer-Setup` AP
6. Reconfigure with correct credentials
7. Device should reboot and connect successfully

### Test 3: WiFi Network Unavailable
1. Configure device with valid credentials
2. Turn off the WiFi router
3. Reboot device
4. Device should fail to connect and start captive portal
5. Connect to `AudioStreamer-Setup` AP
6. Verify web interface is accessible at http://192.168.4.1

## Expected Behavior After Fix

1. **First Boot**: Captive portal starts automatically (with timeout)
2. **WiFi Connection Failure**: Captive portal starts automatically (no timeout)
3. **Successful WiFi Connection**: Normal operation, no captive portal
4. **Invalid Credentials**: Captive portal provides recovery mechanism instead of boot loop

## Additional Notes

- The captive portal will now activate whenever WiFi connection fails, providing a better user experience
- No timeout when captive portal is triggered by WiFi failure - user must configure or manually power cycle
- DNS server redirects all queries to 192.168.4.1 for proper captive portal detection
- Web interface available at http://192.168.4.1 when in AP mode
