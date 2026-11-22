# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A KiCad 10 hardware project (no firmware or code here): a 2-layer THT carrier PCB for an "environment monitor" built around an **ESP32-S3 SuperMini** module. Single-sheet schematic (`environment_monitor.kicad_sch`) and its board (`environment_monitor.kicad_pcb`). Files are KiCad S-expressions; edit them through KiCad where possible. Hand edits must keep UUIDs and schematic/PCB parity intact.

`.history/` is KiCad's automatic local-history snapshot repo (its own internal git). Don't edit it. The project directory itself is not under version control.

## Commands (kicad-cli 10.x)

```sh
kicad-cli sch erc --severity-all -o erc.rpt environment_monitor.kicad_sch
kicad-cli pcb drc --severity-all --schematic-parity -o drc.rpt environment_monitor.kicad_pcb
kicad-cli sch export netlist --format kicadsexpr -o out.net environment_monitor.kicad_sch   # inspect net→pin mapping
kicad-cli pcb export gerbers -o gerbers/ environment_monitor.kicad_pcb
kicad-cli pcb export drill   -o gerbers/ environment_monitor.kicad_pcb
```

Write reports and exports to a scratch location, not into the project directory.

## Design overview

Connectivity is done with **global labels**, not wires between blocks. To trace a signal, follow the label name.

- **U1 ESP32-S3 SuperMini** supplies power: USB 5V goes to `+5V`, and its onboard regulator feeds `+3V3`.
- **J1 (1x14 socket): SPI TFT with resistive touch** (ILI9341-style pinout: VCC, GND, CS, RST, DC, MOSI, SCK, LED, MISO, T_CLK, T_CS, T_DIN, T_DO, T_IRQ). The display and touch controller share one SPI bus on the S3's FSPI IOMUX pins: CS=GPIO10, MOSI=GPIO11, SCK=GPIO12, MISO=GPIO13. DC=GPIO9, RST=GPIO8, TOUCH_CS=GPIO1, TOUCH_IRQ=GPIO7. Module VCC is on +5V; backlight LED is tied straight to +3V3.
- **I2C bus**: SDA=GPIO5, SCL=GPIO6, with 4k7 pull-ups (R4/R5) to 3V3. It is shared by J2/J3 (4-pin terminal blocks for sensors: 3V3, SDA, SCL, GND) and J4 (6-pin RTC header, DS3231-style: 32K, SQW, SCL, SDA, VCC, GND; 32K and SQW are unused).
- **Buttons** SW1/SW2/SW3 (PLUS/MINUS/SET) go to GPIO2/3/4. Each is active-low with a 10k pull-up to 3V3 (R1–R3).
- GPIO43/44 (UART TX/RX) are deliberately left free.

## Libraries and known quirks

- The ESP32-S3 SuperMini symbol and footprint come from an external library at `~/kicad-ext-libs/ESP32-S3_SuperMini`. It is registered in the *global* lib tables under the nicknames `esp32 s3 supermini` (symbols) and `ESP32 S3 SUPERMINI` (footprints). There are no project-local lib tables, so the project won't resolve U1 on another machine without that library.
- The buttons use the `Switch:SW_Push_Dual` symbol (4 pins) on the `SW_PUSH_6mm` footprint, whose pads are numbered 1,1,2,2. Because of that, ERC flags symbol pins 3/4 as unconnected and DRC parity warns "No pad found for pin 3/4". This is expected; the switched contact is pin 1 (signal) to pin 2 (GND).
- There are no PWR_FLAG symbols yet, so ERC reports `power_pin_not_driven` on +5V/+3V3/GND.
- PCB: all tracks are 0.2 mm. There is a GND pour on B.Cu. Board min clearance is 0.5 mm.
