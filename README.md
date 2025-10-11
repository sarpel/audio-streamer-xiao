# ESP32-S3 Audio Streamer

A compact audio streaming device using the Seeed XIAO ESP32-S3 that captures audio from an INMP441 microphone and streams it over WiFi to a TCP server.

## Features

- **Real-time Audio Capture**: 16-bit @ 16kHz mono audio from INMP441 microphone
- **WiFi Streaming**: Reliable TCP connection with automatic reconnection
- **Web Interface**: Configuration and monitoring via responsive web UI
- **OTA Updates**: Wireless firmware updates
- **Compact Design**: Fits on Seeed XIAO ESP32-S3 board

## Hardware Requirements

- Seeed XIAO ESP32-S3
- INMP441 microphone module
- Power supply (5V USB)

### Pin Connections

| ESP32-S3 Pin | INMP441 Pin | Function |
|-------------|-------------|----------|
| GPIO 1      | DATA OUT    | I2S Data |
| GPIO 2      | BCK         | I2S Clock |
| GPIO 3      | WS/LRCLK    | I2S Word Select |

## Software Installation

### Prerequisites

1. Install [PlatformIO](https://platformio.org/) IDE or CLI
2. Clone this repository

### Configuration

1. Open the project in PlatformIO
2. Edit `src/config.h` to set your WiFi credentials and server IP:

    ```cpp
    #define WIFI_SSID "your_wifi_name"
    #define WIFI_PASSWORD "your_wifi_password"
    #define TCP_SERVER_IP "192.168.1.100"  // Your server IP
    #define TCP_SERVER_PORT 9000
    ```

### Build and Upload

```bash
# Build the project
pio run

# Upload to device
pio run --target upload --environment xiao_esp32s3

# Monitor serial output
pio device monitor
```

## Usage

### First Time Setup

1. Power on the device
2. It will create a WiFi access point called "AudioStreamer-Setup"
3. Connect to this network and open http://192.168.4.1
4. Configure your WiFi settings through the web interface

### Normal Operation

Once configured, the device will:

- Connect to your WiFi network
- Start streaming audio to the configured TCP server
- Provide a web interface at http://audiostreamer.local (or device IP)

### Web Interface

Access the web interface at:

- http://audiostreamer.local
- Or the device's IP address

Features include:

- **Dashboard**: Real-time status and statistics
- **Configuration**: WiFi, audio, and server settings
- **Monitoring**: Buffer usage, connection status, system info
- **OTA Updates**: Firmware update capability

## TCP Server

The device streams raw 16-bit PCM audio data over TCP. The server should:

1. Listen on the configured port (default: 9000)
2. Accept raw audio data (16-bit little-endian, 16kHz, mono)
3. Process or save the audio stream as needed

## Troubleshooting

### Device won't connect to WiFi

- Check WiFi credentials in `src/config.h`
- Ensure the device is in range of the WiFi network
- Use the captive portal setup if needed

### No audio streaming

- Verify TCP server is running and accessible
- Check network connectivity
- Monitor serial output for error messages

### Web interface not accessible

- Ensure device is connected to network
- Try accessing via IP address instead of hostname
- Check firewall settings

## Development

### Project Structure

```
src/
├── main.cpp              # Application entry point
├── config.h              # Configuration settings
└── modules/              # Functional modules
    ├── i2s_handler.cpp   # Audio capture
    ├── tcp_streamer.cpp  # Network streaming
    ├── web_server.cpp   # HTTP interface
    └── ...
```

### Building for Development

```bash
# Clean build
pio run --target clean

# Build with debug symbols
pio run --environment xiao_esp32s3

# Upload and monitor
pio run --target upload --target monitor
```

## License

This project is open source. Please check individual files for license information.

## Support

For issues and questions, please check the troubleshooting section above or create an issue in the repository.
