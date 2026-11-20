# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build & Flash

Using `arduino-cli` (preferred for scripting):

```bash
# Compile
arduino-cli compile --fqbn esp32:esp32:esp32s3 environment_monitor.ino

# Upload (replace /dev/ttyUSB0 with actual port)
arduino-cli upload --fqbn esp32:esp32:esp32s3 -p /dev/ttyUSB0 environment_monitor.ino

# Monitor serial output
arduino-cli monitor -p /dev/ttyUSB0 -c baudrate=115200
```

Required libraries (install via `arduino-cli lib install "<name>"`):
- `Adafruit GFX Library`
- `Adafruit ILI9341`
- `Adafruit SHT4x Library`
- `NimBLE-Arduino`

Board core: `esp32:esp32` (Espressif ESP32 Arduino core).

## Architecture

Single `.ino` file, no build system beyond Arduino. No WiFi — time/date is set over BLE and kept by a DS3231M RTC when present.

**Time setting (BLE):** GATT server advertises as `EnvMonitor`. Writing to `BLE_TIME` (`...def1`) sets date+time; accepted formats: Unix epoch seconds (UTC, e.g. `1763650000`) or local time `YYYY-MM-DD HH:MM[:SS]` (CET/CEST). Reading `BLE_TIME` returns the current epoch (`0` if unset). `BLE_STAT` (`...def2`) notifies `ok:rtc`, `ok:local` or `err:format`. The BLE callback only stores the value; I2C work happens in `loop()` (`applyPendingTime`) because the bus is shared with the SHT4x.

**RTC (DS3231M @ 0x68):** Minimal register-level driver over `Wire`, no library. RTC stores UTC (24h mode); local time comes from the `TZ` env var. On boot: if the RTC answers and its OSF flag is clear, the system clock is loaded from it; otherwise time stays unset (`--:--`). Without an RTC, time lives only in the ESP32 system clock and is lost on power loss. The system clock is re-synced from the RTC every hour. A time write re-probes the RTC, so a later-connected RTC gets picked up.

**Display strategy:** `drawMain()` avoids `fillScreen` on every frame. The static frame is drawn once; each data region (T, rH, comfort bar, clock, date, status line) is redrawn only when its value changes, using fixed-width text so new text overwrites old.

## Hardware

| Signal  | ESP32-S3 pin |
|---------|-------------|
| TFT CS  | 10          |
| TFT DC  | 9           |
| TFT RST | 8           |
| TFT MOSI| 11          |
| TFT SCK | 13          |
| TFT MISO| 12          |
| I2C SDA | 5           |
| I2C SCL | 6           |

Display: ILI9341, 320×240, rotation 1 (landscape). The SHT4x sensor and the optional DS3231M RTC share the I2C bus (25 kHz).

**Note:** the TFT uses hardware SPI (`SPI` default pins), the MOSI/SCK/MISO defines are informational.
