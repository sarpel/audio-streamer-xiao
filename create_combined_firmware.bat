@echo off
REM filepath: d:\audio-streamer-xiao\create_combined_firmware.bat
echo Creating combined firmware...

set BUILD_DIR=.pio\build\xiao_esp32s3
set BOOTLOADER=%BUILD_DIR%\bootloader.bin
set PARTITIONS=%BUILD_DIR%\partitions.bin
set FIRMWARE=%BUILD_DIR%\firmware.bin
set COMBINED=%BUILD_DIR%\firmware-combined.bin

REM Check if files exist
if not exist "%BOOTLOADER%" (
    echo ERROR: bootloader.bin not found!
    exit /b 1
)

if not exist "%PARTITIONS%" (
    echo ERROR: partitions.bin not found!
    exit /b 1
)

if not exist "%FIRMWARE%" (
    echo ERROR: firmware.bin not found!
    exit /b 1
)

REM Merge using esptool.py
python -m esptool --chip esp32s3 merge_bin -o "%COMBINED%" ^
    --flash_mode dio ^
    --flash_freq 80m ^
    --flash_size 8MB ^
    0x0 "%BOOTLOADER%" ^
    0x8000 "%PARTITIONS%" ^
    0x10000 "%FIRMWARE%"

if %ERRORLEVEL% EQU 0 (
    echo.
    echo ✓ Combined firmware created successfully!
    echo   Location: %COMBINED%
    dir "%COMBINED%" | findstr /C:".bin"
) else (
    echo.
    echo ✗ Failed to create combined firmware!
    exit /b 1
)

pause