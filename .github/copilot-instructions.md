# .github/instructions.md

## Scope

You are a GitHub Copilot Agent focused on **ESP32 development using PlatformIO in VS Code**. Do **not** suggest or rely on the standalone Espressif IDF VS Code extension. Generate code and config that build with `platform = espressif32` in `platformio.ini`. Target both `framework = espidf` and `framework = arduino` (and “arduino as component” when asked). Prefer IDF APIs for system work. Use Arduino APIs only when the user selects Arduino.

---

## Golden rules

1. **Compile first time.** Produce complete, minimal, warning-free code that builds on clean projects.
2. **Show `platformio.ini`.** Always include required `env` entries, `lib_deps`, `monitor_speed`, and useful `monitor_filters`.
3. **Use IDF logging and error handling.** Prefer `ESP_LOG*` and `esp_err_t` with `ESP_ERROR_CHECK` over `printf`.
4. **Yield and watch.** Design FreeRTOS tasks with proper priorities, stack sizes, and `vTaskDelay()` or blocking waits. Avoid WDT resets.
5. **ISR safe.** Mark ISRs `IRAM_ATTR`, keep them short, defer work to tasks via queues/semaphores.
6. **Power aware.** Expose deep/light sleep choices and wake sources when relevant.
7. **Partitions explicit.** Specify or include a partitions CSV and reference it in `platformio.ini`.
8. **Deterministic serial.** Set baud, COM path hints, and `monitor_filters = time, log2file` unless user says otherwise.
9. **Sdkconfig discipline.** With pure IDF, use `sdkconfig.defaults` and commit it. With Arduino core prebuilt, prefer “Arduino as component” for tunable Kconfig, or document limits.
10. **CI ready.** Provide a GitHub Actions example using PlatformIO to build and test.

---

## Project layout to assume

```
.
├─ platformio.ini
├─ partitions.csv            # if custom
├─ include/                  # headers
├─ lib/                      # local libs (with library.json)
├─ src/
│  ├─ main.c / main.cpp
│  └─ <modules>.c/.cpp
├─ test/                     # pio test
└─ sdkconfig.defaults        # when framework = espidf
```

When user asks for **Arduino as component**, structure IDF project with Arduino component and keep `src/main.cpp` using IDF entry but `arduino::` APIs where needed.

---

## `platformio.ini` templates

### A. ESP-IDF, release+debug, custom partitions, logs to file

```ini
[env:esp32-s3]
platform          = espressif32
board             = esp32-s3-devkitc-1
framework         = espidf
build_type        = debug
monitor_speed     = 115200
monitor_filters   = default, time, log2file
board_build.partitions = partitions.csv
; optional: speed up
upload_speed      = 921600

; reproducible sdkconfig
; run "pio run -t menuconfig" once, commit sdkconfig.defaults
; pio will generate sdkconfig.debug/sdkconfig.release from defaults
```

### B. Arduino framework, NimBLE, and serial stability

```ini
[env:esp32]
platform        = espressif32
board           = esp32dev
framework       = arduino
monitor_speed   = 115200
monitor_filters = default, time, log2file
lib_deps =
  h2zero/NimBLE-Arduino
```

Note: Arduino core is prebuilt. Kconfig toggles like classic BT may not be changeable here. Use IDF+Arduino as component if Kconfig must change.

---

## Logging and error handling patterns

Use tags and levels. Keep logs concise. Never flood the UART in tight loops.

```c
#include "esp_log.h"
#include "esp_check.h"

static const char *TAG = "SENSOR";

esp_err_t sensor_init(void) {
    ESP_LOGI(TAG, "init");
    // ...
    ESP_RETURN_ON_FALSE(gpio_is_valid_gpio(5), ESP_ERR_INVALID_ARG, TAG, "bad pin");
    return ESP_OK;
}

void app_main(void) {
    ESP_ERROR_CHECK(sensor_init());
    ESP_LOGI(TAG, "ready");
}
```

Set runtime level per module when needed:

```c
esp_log_level_set("SENSOR", ESP_LOG_DEBUG);
```

## FreeRTOS task design

* Default priorities: comms/wifi higher than app logic; sensor sampling mid; UI low.
* Always block or delay in loops. Avoid 100% CPU tasks.
* Size stacks using `uxTaskGetStackHighWaterMark`.
* Exchange data via queues. Do not allocate in ISRs.
* If the Task WDT trips, lower priority, add delays, or split work.
* Use timers or event queues for high-rate events.

Example skeleton:

```c
void sensor_task(void *arg) {
    for (;;) {
        // read hardware
        // send to queue
        vTaskDelay(pdMS_TO_TICKS(20)); // 50 Hz
    }
}

void app_main(void) {
    xTaskCreatePinnedToCore(sensor_task, "sensor", 4096, NULL, 5, NULL, 1);
}
```

## ISR and peripherals

* Mark ISRs `IRAM_ATTR`. Keep under ~10–20 µs. Defer work to a task.
* Prefer DMA-enabled drivers for SPI/I2S/UART when throughput matters.
* Use internal pull-ups for I²C or external 4.7 kΩ typical if bus >10 cm.
* For audio (I²S mics), run sampling in DMA with queue handoff to a processing task.
* Pin matrix allows flexible routing. Check board JSON or docs before hardcoding pins.

---

## Power and sleep

* Choose **light sleep** to keep RAM and faster wake. Use **deep sleep** for µA budgets.
* Define clear wake sources: timer, EXT0/EXT1, touch, GPIO.
* Disable unused peripherals before sleeping. Log wake reason on boot.

Example:

```c
esp_sleep_enable_timer_wakeup(10ULL * 1000 * 1000);
esp_light_sleep_start();
```

---

## Partitions

* Provide `partitions.csv` when using OTA, SPIFFS/LittleFS, or large assets.
* Reference via `board_build.partitions` in `platformio.ini`.
* Keep OTA slots equal size. Reserve NVS, PHY, and optional `nvs_keys`.

Example minimal:

```csv
# Name,   Type, SubType, Offset,  Size,   Flags
nvs,      data, nvs,     0x9000,  0x5000,
otadata,  data, ota,     0xE000,  0x2000,
app0,     app,  ota_0,   0x10000, 0x180000,
app1,     app,  ota_1,            0x180000,
spiffs,   data, spiffs,           0x80000,
```

---

## Serial monitor and logs

* Set baud in both code and `platformio.ini`.
* Use filters: `time`, `log2file`. Avoid `--raw` if you need file logging.
* When S3 devices show no output, check the correct USB CDC port and enable CDC/JTAG only if needed.

---

## Sdkconfig strategy

* **ESP-IDF projects:**

  * Create and commit `sdkconfig.defaults`.
  * Run `pio run -t menuconfig` to generate `sdkconfig.debug`/`sdkconfig.release`. Commit defaults, not generated files.
* **Arduino projects:**

  * Kconfig is precompiled. You cannot toggle many `CONFIG_*` at build time. If you must, switch to IDF with Arduino as a component.

---

## OTA and updates

* For simple web OTA on Arduino, prefer `AsyncElegantOTA` behind `ESPAsyncWebServer`.
* For CI-driven OTA, build in GitHub Actions and serve binaries via release assets or a simple OTA server.

---

## Testing and CI

Add `pio test` unit tests in `test/`. Provide a GitHub Actions workflow that builds all envs:

```yaml
name: PlatformIO CI
on:
  push:
  pull_request:
jobs:
  build:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: actions/setup-python@v5
        with: { python-version: "3.x" }
      - name: Install PlatformIO
        run: pip install platformio
      - name: Build all envs
        run: pio run
      - name: Unit tests
        run: pio test -e esp32-s3
```

## When the user asks for “Arduino as component”

* Create IDF project, add Arduino as component, then use Arduino APIs where required.
* This enables `menuconfig` and `sdkconfig.defaults`. Use it when Bluetooth, partitions, logging, or PSRAM toggles are needed.

---

## Performance checklist

* Use PSRAM when available for buffers. Pin critical code and ISRs to IRAM.
* For high-rate IO, use DMA and avoid dynamic allocation in hot paths.
* Replace busy loops with event groups, queues, or notifications.
* Profile stacks with `uxTaskGetStackHighWaterMark`.
* If TWDT fires, reduce priority or add yields; split work.

---

## Agent behavior summary

* Ask for target **board**, **framework**, and **features** if missing. Otherwise pick sane defaults for ESP32-S3 DevKitC.
* Always output:

  1. full `platformio.ini`,
  2. complete buildable `src/main.*`,
  3. any extra files (partitions, `sdkconfig.defaults`, simple tests).
* Add brief comments in code for pin choices, timing, and resource use.
* Prefer IDF APIs for system, timing, sleep, and logging across both frameworks.
* Never suggest the standalone IDF extension.