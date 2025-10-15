"""
PlatformIO pre-build script to enable ESP-IDF component manager
Required for ping component from ESP-IDF component registry
"""
Import("env")
import os

# Enable component manager (needed for esp_ping component)
env['ENV']['IDF_COMPONENT_MANAGER'] = '1'
os.environ['IDF_COMPONENT_MANAGER'] = '1'

print("Component manager enabled (required for esp_ping component)")
