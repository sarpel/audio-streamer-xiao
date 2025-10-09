# Security Policy

## Overview

The ESP32-S3 Audio Streamer implements security measures appropriate for a local network IoT device. This document outlines security features, best practices, and responsible disclosure procedures.

## Security Features

### Authentication

- **HTTP Basic Authentication**: All API endpoints require authentication
- **Credential Storage**: Credentials stored in NVS (Non-Volatile Storage)
- **Default Credentials**: Must be changed on first boot via Web UI

### Input Validation

- **IP Address Validation**: All IPv4 addresses validated for correct format
- **Port Validation**: Port numbers checked for valid range (1-65535)
- **Sample Rate Validation**: Only supported sample rates accepted
- **String Length Validation**: Prevents buffer overflow attacks
- **Buffer Size Validation**: Enforces reasonable memory limits

### Network Security

- **No External Exposure**: Designed for trusted local networks only
- **Authenticated Endpoints**: All configuration endpoints require auth
- **CORS Protection**: Web server doesn't allow cross-origin requests by default

## Default Credentials

⚠️ **IMPORTANT**: Change these immediately after first boot!

**WiFi Access Point** (if captive portal enabled):
- SSID: `ESP32-AudioStreamer`
- Password: `changeme123`

**Web Interface**:
- Username: `admin`
- Password: `admin123`

## Changing Credentials

### Via Web UI (Recommended)

1. Connect to device's WiFi network or access via local IP
2. Navigate to `http://<device-ip>/config.html`
3. Update WiFi credentials in WiFi section
4. Update web authentication in Auth section (if available)
5. Save and restart

### Via Source Code (Before First Flash)

Edit `src/config.h`:

```cpp
#define WIFI_SSID "YourNetworkName"
#define WIFI_PASSWORD "YourSecurePassword"
#define WEB_AUTH_USERNAME "yourusername"
#define WEB_AUTH_PASSWORD "yourstrongpassword"
```

⚠️ **Never commit real credentials to version control!**

## Security Best Practices

### For Home/Lab Use

1. **Change Default Credentials**: Immediately after first boot
2. **Use Strong Passwords**: Minimum 12 characters with mixed case, numbers, symbols
3. **Isolated Network**: Consider placing on separate VLAN/network segment
4. **Regular Updates**: Check for firmware updates periodically
5. **Monitor Logs**: Review web UI logs for unusual activity

### For Production/Internet Deployment

⚠️ **Additional measures required**:

1. **Reverse Proxy with HTTPS**: Use nginx or caddy with SSL/TLS
   ```nginx
   server {
       listen 443 ssl;
       server_name audiostreamer.example.com;
       
       ssl_certificate /path/to/cert.pem;
       ssl_certificate_key /path/to/key.pem;
       
       location / {
           proxy_pass http://<device-ip>;
           proxy_set_header Authorization $http_authorization;
       }
   }
   ```

2. **Rate Limiting**: Implement at reverse proxy level
   ```nginx
   limit_req_zone $binary_remote_addr zone=api:10m rate=10r/s;
   ```

3. **VPN Access**: Use WireGuard or OpenVPN instead of direct internet exposure

4. **Firewall Rules**: Restrict access to known IP ranges

5. **Authentication Enhancement**: 
   - Implement session tokens
   - Add password hashing (SHA256 minimum)
   - Consider OAuth2 or JWT tokens

## Known Limitations

### Current Implementation

- ❌ **No HTTPS**: HTTP only (plaintext)
- ❌ **No Rate Limiting**: Built-in rate limiting not implemented
- ❌ **Plaintext Passwords**: Credentials stored in plain text in NVS
- ❌ **No Session Management**: Basic Auth sent with every request
- ❌ **No CSRF Protection**: Not implemented
- ❌ **No Audit Logging**: Limited security event logging

### Not Suitable For

- ✗ Direct internet exposure without reverse proxy
- ✗ Untrusted networks
- ✗ Environments requiring compliance (HIPAA, PCI-DSS, etc.)
- ✗ Multi-tenant deployments
- ✗ Public installations

### Suitable For

- ✓ Home networks with trusted users
- ✓ Laboratory environments
- ✓ Development/testing
- ✓ Behind VPN
- ✓ Behind authenticated reverse proxy

## Threat Model

### In Scope

- **Local Network Attackers**: Someone on the same WiFi network
- **Misconfiguration**: Accidental security misconfigurations
- **Input Validation**: Malformed API requests
- **Denial of Service**: Resource exhaustion attacks

### Out of Scope

- **Physical Access**: Physical security is user's responsibility
- **WiFi Security**: WPA2/WPA3 security is handled by router
- **Supply Chain**: Hardware component security
- **Side Channel**: Timing attacks, power analysis

## Reporting Security Issues

### Responsible Disclosure

If you discover a security vulnerability:

1. **DO NOT** create a public GitHub issue
2. **DO NOT** share details publicly until patched
3. **DO** email the maintainer privately (see GitHub profile)
4. **DO** provide:
   - Description of vulnerability
   - Steps to reproduce
   - Proof of concept (if available)
   - Suggested fix (optional)

### Response Timeline

- **24 hours**: Initial acknowledgment
- **7 days**: Preliminary assessment
- **30 days**: Fix development and testing
- **Coordinated disclosure**: After fix is released

### Bug Bounty

Currently no bug bounty program. Security research is appreciated and will be acknowledged in release notes (with permission).

## Security Updates

### Update Notification

- Check GitHub releases: https://github.com/sarpel/audio-streamer-xiao/releases
- Monitor GitHub issues with `security` label
- Subscribe to repository notifications

### Update Process

1. Download latest firmware from releases
2. Access Web UI → OTA Update page
3. Upload firmware binary
4. Monitor update progress
5. Verify functionality after restart

Alternatively, flash via USB:
```bash
pio run --target upload --environment xiao_esp32s3
```

## Compliance and Regulations

### Privacy

- **No Cloud Connection**: All data stays on local network
- **No Analytics**: No usage tracking or telemetry
- **No User Data**: Device doesn't collect personal information

### GDPR Compliance

Not applicable - device processes audio streams locally without storing personal data.

### FCC/CE Compliance

ESP32-S3 module is FCC/CE certified. Complete device certification is integrator's responsibility.

## Additional Resources

- [ESP-IDF Security Features](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/security/index.html)
- [OWASP IoT Top 10](https://owasp.org/www-project-internet-of-things/)
- [NIST IoT Security Guidelines](https://www.nist.gov/itl/applied-cybersecurity/nist-cybersecurity-iot-program)

## Version History

- **v1.1.0** (2025-10-09): Added input validation, safe default credentials
- **v1.0.0** (2025-10-08): Initial release with basic authentication

---

**Last Updated**: 2025-10-09  
**Security Contact**: See GitHub repository
