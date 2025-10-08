"""
PlatformIO pre-build script to disable ESP-IDF component manager
Workaround for pydantic bug in ESP-IDF 5.5.0
"""
Import("env")
import os

# Set environment variable to disable component manager
env['ENV']['IDF_COMPONENT_MANAGER'] = '0'
os.environ['IDF_COMPONENT_MANAGER'] = '0'

print("Component manager disabled (workaround for ESP-IDF 5.5.0 bug)")
