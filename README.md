# EnvMonitor

ESP32-S3 environment monitor with a 2.8" TFT display showing temperature, humidity, clock and date. WiFi credentials are provisioned over BLE from an Android app — no captive portal, no hardcoded SSID.

```
fw/                        # Arduino firmware (.ino)
app/mobile_app/            # Qt/QML Android app (BLE provisioner)
app/build_android.sh       # CLI build + sign script
```

---

## Hardware

| Component | Details |
|-----------|---------|
| MCU | ESP32-S3 Supermini |
| Display | ILI9341 2.8" TFT (320×240, SPI) |
| Sensor | SHT41 temp/humidity (I2C) |

### Wiring

| Signal | GPIO |
|--------|------|
| TFT CS | 10 |
| TFT DC | 9 |
| TFT RST | 8 |
| TFT MOSI | 11 |
| TFT SCK | 13 |
| TFT MISO | 12 |
| I2C SDA (SHT41) | 5 |
| I2C SCL (SHT41) | 6 |

---

## Firmware (`fw/`)

Single `.ino` file, no build system beyond Arduino.

### Libraries

Install via **Arduino Library Manager** or `arduino-cli lib install "<name>"`:

- `Adafruit GFX Library`
- `Adafruit ILI9341`
- `Adafruit SHT4x Library`
- `NimBLE-Arduino`

Board core: **esp32:esp32** (Espressif ESP32 Arduino core).

### Build with arduino-cli

```bash
# Compile
arduino-cli compile --fqbn esp32:esp32:esp32s3 fw/environment_monitor/environment_monitor.ino

# Upload (replace /dev/ttyUSB0 with your port)
arduino-cli upload --fqbn esp32:esp32:esp32s3 -p /dev/ttyUSB0 fw/environment_monitor/environment_monitor.ino

# Serial monitor
arduino-cli monitor -p /dev/ttyUSB0 -c baudrate=115200
```

### Build with Arduino IDE

Open `fw/environment_monitor/environment_monitor.ino`, select board **ESP32S3 Dev Module**, and upload.

### State machine

```
ST_PROVISION  →  ST_CONNECTING  →  ST_ONLINE
     ↑                                  |
     └──────── WiFi drops / BLE write ──┘
```

| State | Behaviour |
|-------|-----------|
| `ST_PROVISION` | No saved credentials — BLE advertising, display shows "WiFi Setup" |
| `ST_CONNECTING` | 15 s WiFi connect attempt, triggered by BLE credential write |
| `ST_ONLINE` | WiFi up, NTP synced (CET/CEST), display refreshes at 1 Hz, sensor polled every 5 s |

Display updates are diff-based: only the region that changed (clock, temperature, humidity, comfort bar) is redrawn — no full `fillScreen` on every frame.

### Firmware internals

**`environment_monitor.ino`** — single-file, no classes beyond the two NimBLE callback structs.

| Symbol | Role |
|--------|------|
| `appState` | Current state machine state (`ST_PROVISION / ST_CONNECTING / ST_ONLINE`) |
| `doConnect` | `volatile bool` flag set by the BLE write callback; consumed in `loop()` to trigger a WiFi connect from the main core |
| `tempC / rhPct` | Last sensor reading (`NAN` until first successful read) |
| `charStat` | Pointer to the BLE status characteristic; used by `bleNotify()` to send status strings |
| `sensorOk` | `false` if `sht4.begin()` failed — sensor readings are skipped and `--` is shown on display |

**BLE callbacks**

- `CredCB::onWrite` — parses the JSON payload with a minimal `jsonField()` helper (no JSON library needed), saves SSID/password to NVS, then sets `doConnect = true`
- `SrvCB::onDisconnect` — restarts advertising so the device is discoverable again after a client disconnects

**Display layout (landscape 320×240)**

```
 ┌─────────────────────────────────────┐
 │ °C  [temp 5×]        [humidity 6×] %│  ← row 0–70
 │          ▼ humidity marker           │  ← row 73–109
 │ [DRY ░░░][COMFORT ░░░░░][WET ░░░░░] │  ← comfort bar row 110–136
 ├─────────────────────────────────────┤  ← separator row 143
 │ AM  HH:MM              DD/MM        │  ← clock + date row 148–188
 └─────────────────────────────────────┘
```

Each region tracks its last-drawn value and redraws only on change:
- Temperature: redraws when value changes by ≥ 0.05 °C
- Humidity: redraws when value changes by ≥ 0.05 %
- Comfort bar + triangle marker: redraws when humidity changes by ≥ 0.5 %
- Clock: redraws when the minute changes
- Date: redraws when the day changes

**Comfort bar thresholds**

| Zone | Humidity range | Color |
|------|---------------|-------|
| DRY | 0–30 % | Amber |
| COMFORT | 30–60 % | Green |
| WET | 60–100 % | Blue |

A white triangle marker points to the current humidity value along the bar.

---

## Android app (`app/mobile_app/`)

Qt/QML app that scans for the `EnvMonitor` BLE device and sends WiFi credentials.

### Requirements

- Qt 6.5+ (uses `QBluetoothPermission` and `qt_standard_project_setup`)
- Android NDK + SDK configured in Qt Creator

### Build in Qt Creator

**File → Open Project → `app/mobile_app/CMakeLists.txt`**, then **Run → Android device**.

### Build from command line

```bash
cd app
./build_android.sh
```

The script reads Qt/SDK paths from `$HOME/Qt` and `$HOME/Android/Sdk`. For a release build supply your own keystore:

```bash
KEYSTORE=/path/to/release.keystore \
KS_PASS=yourpassword \
KS_ALIAS=youralias \
./build_android.sh
```

> The script defaults to the Android debug keystore (`~/.android/debug.keystore`, password `android`). Fine for development — use a real keystore for Play Store releases.

---

## BLE provisioning protocol

The Android app and the firmware share these UUIDs and JSON format:

| Role | UUID |
|------|------|
| Service | `12345678-1234-5678-1234-56789abcdef0` |
| Credentials (WRITE) | `12345678-1234-5678-1234-56789abcdef1` |
| Status (READ \| NOTIFY) | `12345678-1234-5678-1234-56789abcdef2` |

**Client → device** (write to credentials characteristic):
```json
{"ssid": "MyNetwork", "pass": "hunter2"}
```

**Device → client** (notification on status characteristic):

| Value | Meaning |
|-------|---------|
| `connecting` | Connect attempt started |
| `ok:<ip>` | Connected — IP address included |
| `fail` | Wrong SSID or password |

Credentials are stored in NVS via `Preferences` and survive reboots.
