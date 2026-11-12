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

Single `.ino` file, no build system beyond Arduino. The firmware runs a 3-state machine:

```
ST_PROVISION  →  ST_CONNECTING  →  ST_ONLINE
     ↑                                  |
     └──────── WiFi drops / BLE write ──┘
```

**Provisioning (ST_PROVISION):** BLE GATT server advertises as `EnvMonitor`. A mobile app writes `{"ssid":"...","pass":"..."}` JSON to the credential characteristic (`BLE_CRED`). The status characteristic (`BLE_STAT`) notifies back `connecting`, `ok:<ip>`, or `fail`. Credentials are persisted in NVS via `Preferences` under the `"wifi"` namespace.

**Online (ST_ONLINE):** NTP sync via `configTzTime` (CET/CEST timezone). The main loop redraws the TFT at 1 Hz and polls the SHT4x sensor every 5 s.

**Display strategy:** `drawOnline()` avoids `fillScreen` on every frame. The static frame (separator lines, label text) is drawn once behind `onlineFrameDrawn`. Each data region (clock, date, T, rH, status bar) is redrawn only when its value changes, using a fixed-width format so old text is fully overwritten by the new text.

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

Display: ILI9341, 320×240, rotation 1 (landscape). The SHT4x temperature/humidity sensor is on I2C.

**Known issue:** `PIN_SDA` is defined as `5l` (digit 5 followed by letter l) — this is a typo for `5`. It compiles because `5l` is a valid long literal, but flag it if refactoring pin definitions.
