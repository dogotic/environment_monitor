# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Hardware

| Component | Details |
|-----------|---------|
| MCU | ESP32-S3 Supermini |
| Display | ILI9341 2.8" TFT (320×240) |
| Sensor | SHT41 temp/humidity (I2C) — not yet wired |

**Pin assignments**

| Signal | GPIO |
|--------|------|
| TFT RST | 8 |
| TFT DC | 9 |
| TFT CS | 10 |
| TFT MOSI (SPI2/FSPI) | 11 |
| TFT MISO | 12 |
| TFT SCK | 13 |
| I2C SDA (SHT41) | 5 |
| I2C SCL (SHT41) | 6 |

## Build (PlatformIO)

```bash
pio run -e esp32-s3                   # compile
pio run -e esp32-s3 -t upload         # flash
pio device monitor -b 115200          # serial monitor
```

TFT_eSPI pin config is passed via `build_flags` in `platformio.ini` — do **not** edit the library's `User_Setup.h`.

If using **Arduino IDE** instead of PlatformIO, install the same libraries manually and edit `User_Setup.h` inside the TFT_eSPI library folder to match the pin table above (`TFT_MOSI`, `TFT_MISO`, `TFT_SCLK`, `TFT_CS`, `TFT_DC`, `TFT_RST`, `ILI9341_DRIVER`).

## Architecture

**WiFi provisioning** is done over BLE (NimBLE-Arduino), not WiFiManager captive portal.  
A Web Bluetooth page (see the published Artifact) connects to the BLE device named `EnvMonitor`, writes JSON `{"ssid":"…","pass":"…"}` to the credentials characteristic, and receives a status notification.  
Credentials are stored in NVS via `Preferences`.

**State machine** in `src/main.cpp`:
- `ST_PROVISION` — no saved credentials or first boot; BLE advertising, display shows "WiFi Setup"
- `ST_CONNECTING` — blocking 15 s WiFi connect attempt (triggered from BLE callback via `volatile doConnect` flag)
- `ST_ONLINE` — WiFi connected; NTP synced (`CET-1CEST,M3.5.0,M10.5.0/3`), display refreshes every second

**BLE UUIDs** (must match the web app exactly):
- Service: `12345678-1234-5678-1234-56789abcdef0`
- Credentials characteristic (WRITE): `12345678-1234-5678-1234-56789abcdef1`
- Status characteristic (READ|NOTIFY): `12345678-1234-5678-1234-56789abcdef2`

## Qt Android app (`mobile_app/`)

**Zahtijeva Qt 6.5+** (zbog `QBluetoothPermission` API-a i `qt_standard_project_setup`).

Otvaranje u Qt Creatoru: `File → Open Project → mobile_app/CMakeLists.txt`  
Android kit mora imati NDK i Android SDK konfiguriran.

Build/deploy direktno iz Qt Creatora (Run → Android uređaj).

Ključne klase:
- `BleProvisioner` (C++) — scan, connect, characteristic read/write; expose-an u QML kao `EnvMonitorApp/BleProvisioner`
- `main.qml` — Material UI, prati `BleProvisioner.state` enum: `Idle | Scanning | Connecting | Connected | Error`
- `android/AndroidManifest.xml` — sve BLE permission deklaracije za API 26–34

## Enabling the SHT41 sensor

Search `src/main.cpp` for the comment blocks starting with `// #include <Adafruit_SHT4x.h>` and uncomment all three sections (`#include`, init in `setup()`, read in `loop()`).
