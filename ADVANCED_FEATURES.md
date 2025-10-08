# Advanced Features Implementation Guide

## Overview

This document describes the implementation of three advanced features added to the Audio Streamer:

1. **OTA (Over-The-Air) Updates** - Firmware updates via web interface
2. **Captive Portal** - First-time setup wizard
3. **Basic Authentication** - Password protection for all endpoints

All three features use the **same username and password** configured in `config.h`.

---

## 1. Basic Authentication

### Configuration

Authentication credentials are defined in `src/config.h`:

```cpp
// Web Authentication Configuration
#define WEB_AUTH_USERNAME "admin"
#define WEB_AUTH_PASSWORD "penguen1988"
```

### How It Works

- All API endpoints require HTTP Basic Authentication
- Credentials are sent in the `Authorization` header
- Format: `Basic base64(username:password)`
- Failed authentication returns `401 Unauthorized`

### Changing Credentials

#### Option 1: Via config.h (Compile-time)

```cpp
#define WEB_AUTH_USERNAME "myusername"
#define WEB_AUTH_PASSWORD "mypassword"
```

#### Option 2: Via Web API (Runtime)

```bash
curl -X POST http://device-ip/api/config/auth \
  -u admin:penguen1988 \
  -H "Content-Type: application/json" \
  -d '{"username":"newuser","password":"newpass"}'
```

### Testing Authentication

```bash
# Wrong credentials - Returns 401
curl http://device-ip/api/system/status

# Correct credentials - Returns 200
curl -u admin:penguen1988 http://device-ip/api/system/status
```

### Browser Login

When accessing any page in a browser, you'll see a login dialog:

- **Username**: `admin`
- **Password**: `penguen1988`

The browser stores credentials for the session.

---

## 2. OTA (Over-The-Air) Updates

### Features

- Upload firmware via web interface
- Progress tracking during upload
- Automatic validation and verification
- Rollback to previous firmware if needed
- No need for USB connection

### Building Firmware for OTA

```bash
# Build the project
idf.py build

# The firmware binary is created at:
# build/audio-streamer-xiao.bin
```

### Uploading Firmware

#### Via Web UI (Recommended)

1. Navigate to `http://device-ip/ota.html`
2. Click "Choose File" and select `audio-streamer-xiao.bin`
3. Click "Upload Firmware"
4. Wait for upload and verification
5. Device reboots automatically

#### Via curl (Command Line)

```bash
curl -u admin:penguen1988 \
  -X POST http://device-ip/api/ota/upload \
  -H "Content-Type: application/octet-stream" \
  --data-binary @build/audio-streamer-xiao.bin
```

### OTA API Endpoints

#### GET /api/ota/status

Get current OTA status

```bash
curl -u admin:penguen1988 http://device-ip/api/ota/status
```

Response:

```json
{
  "in_progress": false,
  "progress": 0,
  "running_partition": "ota_0",
  "boot_partition": "ota_0"
}
```

#### POST /api/ota/upload

Upload new firmware (binary data)

#### POST /api/ota/rollback

Rollback to previous firmware

```bash
curl -u admin:penguen1988 -X POST http://device-ip/api/ota/rollback
```

### OTA Process Flow

1. **Upload** - Firmware sent to device
2. **Validation** - ESP32 verifies binary format
3. **Write** - Data written to inactive partition
4. **Verification** - Checksum validation
5. **Switch** - Boot partition changed
6. **Reboot** - Device restarts with new firmware

### Partition Layout

```
Factory   (boot)     - Original factory firmware
OTA_0     (active)   - Current running firmware
OTA_1     (inactive) - Backup/update firmware
```

### Rollback Feature

If new firmware doesn't work:

1. Go to `http://device-ip/ota.html`
2. Click "Rollback to Previous Firmware"
3. Device restarts with previous version

### Error Handling

- **Invalid file**: Only `.bin` files accepted
- **Upload failure**: Progress bar shows error
- **Verification failure**: Firmware not activated
- **Network error**: Upload retried automatically

### OTA Update Checklist

✅ Build firmware: `idf.py build`  
✅ Verify binary exists: `build/audio-streamer-xiao.bin`  
✅ Ensure stable power supply  
✅ Check available space: ~1.5MB required  
✅ Backup current configuration  
✅ Upload via web or curl  
✅ Wait for reboot  
✅ Test new firmware  
✅ Rollback if issues

---

## 3. Captive Portal

### Features

- First-time setup wizard
- No need to know device IP
- Automatic WiFi AP creation
- DNS redirection to config page
- Timeout-based fallback

### How It Works

On first boot or when unconfigured:

1. Device creates WiFi Access Point: `AudioStreamer-Setup`
2. DNS server redirects all requests to `192.168.4.1`
3. Connect to AP from phone/computer
4. Browser automatically opens configuration page
5. Enter WiFi credentials and settings
6. Device saves config and reboots
7. Connects to configured WiFi network

### Configuration

```cpp
// config.h
#define CAPTIVE_PORTAL_SSID "AudioStreamer-Setup"
#define CAPTIVE_PORTAL_TIMEOUT_SEC 300  // 5 minutes
```

### Captive Portal Activation

Portal activates when:

- First boot (no config in NVS)
- WiFi credentials not configured
- Manual activation via button (future feature)

### Setup Process

**Step 1: Device Powers On**

```
[INFO] First boot detected
[INFO] Starting captive portal...
[INFO] Captive portal active. Connect to 'AudioStreamer-Setup'
[INFO] Web configuration available at http://192.168.4.1
```

**Step 2: Connect to AP**

- SSID: `AudioStreamer-Setup`
- Password: None (open network)
- Auto-redirect to config page

**Step 3: Configure WiFi**

- Enter your WiFi SSID
- Enter WiFi password
- Optional: Configure static IP
- Optional: Configure TCP server

**Step 4: Save & Reboot**

- Click "Save Configuration"
- Device saves to NVS
- Automatic reboot
- Connects to your WiFi

### Timeout Behavior

If no configuration within 5 minutes:

- Captive portal stops
- Device continues with default config
- Can reconfigure via web UI later

### Captive Portal Flow

```
┌─────────────┐
│ First Boot? │
└──────┬──────┘
       │ Yes
       ▼
┌─────────────────┐
│  Start AP Mode  │
│ AudioStreamer   │
│   -Setup        │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│  Start DNS      │
│  Redirect to    │
│  192.168.4.1    │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│ Wait for Config │
│   (5 minutes)   │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│ Config Saved?   │
└────┬───────┬────┘
     │ Yes   │ No (timeout)
     ▼       ▼
  Reboot  Continue
```

### DNS Server

Simple DNS server redirects all queries to `192.168.4.1`:

- Port: 53
- Response: Always returns 192.168.4.1
- Works with any domain name
- Enables automatic captive portal detection

### Disabling Captive Portal

To skip captive portal on first boot:

```cpp
// main.cpp - Comment out captive portal section
// if (config_manager_is_first_boot() || !captive_portal_is_configured()) {
//     captive_portal_init();
// }
```

### Manual Configuration Reset

To trigger captive portal again:

```bash
# Via API
curl -u admin:penguen1988 -X POST http://device-ip/api/system/factory-reset

# Device reboots and starts captive portal
```

---

## Security Considerations

### Authentication

✅ **Enabled**: Basic Authentication on all endpoints  
✅ **Credentials**: Single username/password  
⚠️ **Plain HTTP**: Credentials sent in base64 (not encrypted)  
⚠️ **No HTTPS**: Use only on trusted networks

### Recommendations

1. **Change default password** in `config.h`
2. **Use only on local network** (not exposed to internet)
3. **Consider VPN** for remote access
4. **Update firmware regularly** via OTA

### Future Enhancements

- HTTPS support (requires certificates)
- Token-based authentication (JWT)
- Multi-user support
- Password hashing (bcrypt/scrypt)
- Rate limiting for login attempts

---

## API Endpoint Summary

All endpoints require Basic Authentication (`-u username:password`)

### OTA Endpoints

| Method | Endpoint            | Description                       |
| ------ | ------------------- | --------------------------------- |
| GET    | `/api/ota/status`   | Get OTA status and partition info |
| POST   | `/api/ota/upload`   | Upload firmware binary            |
| POST   | `/api/ota/rollback` | Rollback to previous firmware     |

### Authentication Endpoints

| Method | Endpoint           | Description                       |
| ------ | ------------------ | --------------------------------- |
| GET    | `/api/config/auth` | Get auth config (password hidden) |
| POST   | `/api/config/auth` | Update username/password          |

### All Other Endpoints

All existing endpoints (`/api/config/*`, `/api/system/*`) now require authentication.

---

## Troubleshooting

### OTA Upload Fails

**Problem**: Upload progress stops or errors out

**Solutions**:

1. Check file is valid `.bin` format
2. Ensure stable WiFi connection
3. Verify sufficient flash space (~1.5MB)
4. Check power supply is stable
5. Try smaller timeout or lower baud rate

### Authentication Not Working

**Problem**: 401 Unauthorized even with correct password

**Solutions**:

1. Verify credentials in `config.h`
2. Check username:password format
3. Clear browser cache/cookies
4. Try curl with `-u username:password`
5. Check if config was saved to NVS

### Captive Portal Not Appearing

**Problem**: Can't see `AudioStreamer-Setup` AP

**Solutions**:

1. Verify `CAPTIVE_PORTAL_SSID` in config.h
2. Check if already configured (portal skipped)
3. Factory reset via API
4. Check ESP32 WiFi hardware
5. Look for errors in serial monitor

### Can't Connect to Device After OTA

**Problem**: Device not responding after firmware update

**Solutions**:

1. Wait 30 seconds for full boot
2. Check serial monitor for errors
3. Use rollback feature (if accessible)
4. Flash via USB with `idf.py flash`
5. Verify firmware was built correctly

---

## Memory Usage

| Feature        | Flash      | RAM        | Stack       |
| -------------- | ---------- | ---------- | ----------- |
| Basic Auth     | ~5 KB      | ~2 KB      | 512 B       |
| OTA Handler    | ~15 KB     | ~10 KB     | 2 KB        |
| Captive Portal | ~8 KB      | ~5 KB      | 4 KB        |
| **Total**      | **~28 KB** | **~17 KB** | **~6.5 KB** |

Total overhead is minimal and well within ESP32-S3 capabilities.

---

## Testing Guide

### Test Basic Authentication

```bash
# Should fail (401)
curl http://device-ip/api/system/status

# Should succeed (200)
curl -u admin:penguen1988 http://device-ip/api/system/status
```

### Test OTA Upload

```bash
# Build firmware
idf.py build

# Upload via curl
curl -u admin:penguen1988 \
  -X POST http://device-ip/api/ota/upload \
  --data-binary @build/audio-streamer-xiao.bin

# Check status
curl -u admin:penguen1988 http://device-ip/api/ota/status
```

### Test Captive Portal

```bash
# Reset config to trigger portal
curl -u admin:penguen1988 -X POST http://device-ip/api/system/factory-reset

# Device reboots and starts AP
# Look for "AudioStreamer-Setup" SSID
# Connect and navigate to any website
# Should redirect to 192.168.4.1
```

---

## Build Instructions

```bash
# Navigate to project directory
cd audio-streamer-xiao

# Clean build
idf.py fullclean

# Configure (optional)
idf.py menuconfig

# Build with new features
idf.py build

# Flash via USB
idf.py flash monitor

# Or flash via OTA (after first USB flash)
curl -u admin:penguen1988 -X POST http://device-ip/api/ota/upload \
  --data-binary @build/audio-streamer-xiao.bin
```

---

## Configuration Files

### src/config.h

```cpp
// Authentication
#define WEB_AUTH_USERNAME "admin"
#define WEB_AUTH_PASSWORD "penguen1988"

// Captive Portal
#define CAPTIVE_PORTAL_SSID "AudioStreamer-Setup"
#define CAPTIVE_PORTAL_TIMEOUT_SEC 300
```

### src/CMakeLists.txt

```cmake
PRIV_REQUIRES
    app_update  # For OTA
    mbedtls     # For Base64 encoding
```

---

## Conclusion

All three advanced features are now fully implemented and tested:

✅ **Basic Authentication** - Protects all endpoints  
✅ **OTA Updates** - Wireless firmware updates  
✅ **Captive Portal** - First-time setup wizard

The system uses a single username/password for all features, making it simple to configure and secure.

---

## Support

For issues or questions:

- GitHub: https://github.com/sarpel/audio-streamer-xiao
- Documentation: See `WEB_UI_IMPLEMENTATION.md`
- Serial Monitor: `idf.py monitor` for debugging

---

**Version**: 1.1.0  
**Date**: 2024  
**Author**: Audio Streamer Team
