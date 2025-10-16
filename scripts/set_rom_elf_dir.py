import os
from pathlib import Path

Import("env")

# Try to locate ROM ELF directory inside PlatformIO's ESP-IDF package
pkg_dir = env.PioPlatform().get_package_dir("framework-espidf")
rom_candidates = [
    Path(pkg_dir) / "components" / "esp_rom" / "esp32s3",
    Path(pkg_dir) / "components" / "esp_rom" / "rom" / "esp32s3",
]

rom_dir = None
for c in rom_candidates:
    if c.exists():
        rom_dir = str(c)
        break

if rom_dir:
    os.environ["ESP_ROM_ELF_DIR"] = rom_dir
    print(f"Set ESP_ROM_ELF_DIR to: {rom_dir}")
else:
    print("Warning: Could not determine ESP_ROM_ELF_DIR; gdbinit ROM symbols may not be generated.")
