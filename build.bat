@echo off
REM Build script for ESP32-S3 Audio Streamer (Windows)
REM Disables component manager to avoid pydantic bug in ESP-IDF 5.5.0

set IDF_COMPONENT_MANAGER=0

echo Building with component manager disabled...
pio run --environment xiao_esp32s3 %*
