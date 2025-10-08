#!/bin/bash
# Build script for ESP32-S3 Audio Streamer
# Disables component manager to avoid pydantic bug in ESP-IDF 5.5.0

export IDF_COMPONENT_MANAGER=0

echo "Building with component manager disabled..."
pio run --environment xiao_esp32s3 "$@"
