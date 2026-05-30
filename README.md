# CAN-Multitool — Onboard Diagnostic Center on CYD

**Version: 2.2**

[![ESP32](https://img.shields.io/badge/ESP32-2432S028-blue)]()
[![Version](https://img.shields.io/badge/version-2.2-green)](.)
[![License](https://img.shields.io/badge/license-MIT-green)](.)

From a simple CAN ID scanner to a full **onboard diagnostic center**.
One cheap ESP32 display, infinite possibilities.

**Tested on:** Honda Accord 8 (2008–2012), P-CAN 500 kbps

---

## Quick Start

| Step | What to do |
|------|------------|
| 1 | Wire MCP2515+TJA1050 → CYD (SCK=18, MOSI=23, MISO=19, CS=22, 5V=S3, GND=S1) |
| 2 | Connect CAN_H → OBD2 pin 6, CAN_L → OBD2 pin 14 |
| 3 | Open `cyd_can_multitool/cyd_can_multitool.ino` in Arduino IDE |
| 4 | Select board: **ESP32-2432S028** (CYD) |
| 5 | Flash and touch the screen |

| **Libraries required:** |
| `TFT_eSPI` (CYD variant, see `lib/`)
| `MCP_CAN` v2.x (by coryjfowler, see `lib/`)
| `Font7` (built-in TFT_eSPI, 7-segment RLE)
| `FreeSansBold` (built-in TFT_eSPI GFXFF, anti-aliased) |

---

## Hardware

### Wiring

| MCP2515 | CYD | Notes |
|---------|-----|-------|
| VCC (5V) | 5V pad S3 | Solder |
| GND | GND pad S1 | Solder |
| SCK | GPIO 18 | Solder |
| MOSI | GPIO 23 | Solder |
| MISO | GPIO 19 | Solder |
| CS | GPIO 22 | Wire to P3 pin 3 |
| INT | — | Not connected (polling mode) |
| CAN_H | OBD2 pin 6 | — |
| CAN_L | OBD2 pin 14 | — |

---

## Mode Overview

### 1. SCAN ID ✅
Scans the bus for 30 seconds, collects every CAN ID seen, counts how often each ID changes. Results are sorted by change frequency (most active first). Tap any ID → opens Value Monitor.

### 2. ID LIST ✅
Scrollable list of all IDs found during scan. Shows CAN ID + change count. Tap to open Monitor. Returns to main menu via EXIT.

### 3. MONITOR ✅
Watch live raw bytes from any CAN ID. Two entry points:
- **From SCAN**: tap any ID → automatically shows that ID
- **From LIST**: tap any ID → opens monitor

**Display:**
```
MONITOR: 0x158
HEX: 00 11 22 33 44 55 66 77
IDX: 0  1  2  3  4  5  6  7
```
- Hex bytes in FreeSansBold9pt7b
- Byte index labels (FreeSansBold9pt7b)
- Diff-based redraw — only updates when data changes
- Footer: [MENU] button

### 4. SPEEDOMETER ✅
Full-screen digital speedometer and tachometer. Three sub-modes via footer buttons:

| Mode | Display | Source |
|------|---------|--------|
| SPD | Speed km/h | **0x158** BYTE[4:5] (KMH×0.01) |
| RPM | RPM | **0x17C** bytes 2-3 uint16 |
| BTH | Speed + RPM stacked | Both |

Footer: `[SPD] [BTH] [RPM] [MENU]` — active mode highlighted.

**Anti-flicker**: Each value uses a `static` last-drawn tracker — speed and RPM redraw independently. A stationary car with idle RPM only updates the RPM digit, not the zero speed. `fillRect` uses full `SCR_W` (320px) to eliminate font-width artifacts between 3 and 4 digit values.

### 5. DIMMER ✅
Real-time dash brightness monitor from 0x294 SCM_FEEDBACK BYTE[1].

**Display:**
```
         DASH DIMMER

           0x0A        ← big Font7 hex value

  ████████░░░░░░░░░░   ← brightness bar
  0x0A  dim=0  mid=0x15  MAX=0x96
```
- Large 7-segment hex display (Font7)
- Horizontal bar (map 0..150) + live updates via diff-based redraw
- Bar turns red when 0x96 (MAX) is detected
- Footer: [MENU]

### 6. SENSORS ✅
6-cell grid showing live engine sensor values. Tap a cell to assign a CAN ID from scanner buffer.

| Cell | Sensor | CAN ID | Decoder |
|------|--------|--------|---------|
| 1 | Speed | **0x158** | BYTE[4:5]=KMH×0.01 |
| 2 | RPM | **0x17C** | byte[2:3] uint16 |
| 3 | Coolant | **0x324** | byte[0]-40 °C |
| 4 | Fuel | 0x1A6 | byte[3] raw 0-105% |
| 5 | Throttle | 0x13C | byte[0] raw 0-255 |
| 6 | Battery | 0x0 | raw data[byte] |

Cells 1-3 use dedicated decoders (-1 = auto). Cells 4-6 are raw data byte. Tap any empty cell → picker menu with all scanned IDs.

### 7. CAN LOGGER ⏳ planned
### 8. OBD2 SCANNER ⏳ planned
### 9. ECU SCAN ⏳ planned

---

## Menu System

- **Main menu**: 2×2 grid, 8 items on 2 pages
- **Status indicators**: green `+` = implemented, red `-` = planned
- **Navigation**: `<` / `>` footer buttons for page switching
- **Footer bar**: 30px bottom bar with touch buttons, consistent across all modes

```
┌──────────────────────────────────┐
│  ⚫ CAN-MULTITOOL       v2.0     │ ← Header with CAN status dot
├──────────────────────────────────┤
│  [+ SCAN ID]  [+ LIST ID]       │
│  [+ MONITOR]  [+ SPEEDO]        │ ← 2×2 grid
│  [+ SENSORS]  [- LOGGER]        │
│  [- OBD2]     [- PROBE ID]      │
├──────────────────────────────────┤
│  [<]         MENU         [>]    │ ← Footer bar
└──────────────────────────────────┘
```

---

## Honda CAN Architecture (Accord 8, 2008–2012)

| Bus | Type | Speed | Function |
|-----|------|-------|----------|
| **F-CAN (P-CAN)** | HS CAN | **500 kbps** | Powertrain: engine, transmission, ABS, EPS |
| **B-CAN** | HS CAN | **125 kbps** | Body: lights, wipers, locks, AC. Dimmer signal relayed to P-CAN via gauge cluster gateway |

**Gateway (Instrument Cluster)**: relays selective signals:
- F→B: Speed (0x158), coolant temp
- B→F: Ignition, seatbelts, steering wheel buttons (0x296 SCM_BUTTONS)

### Known P-CAN IDs (Accord 8)

| CAN ID | Name | Data |
|--------|------|------|
| **0x158** | ENGINE_DATA | BYTE[0:1]=RPM, BYTE[2:3]=??? , **BYTE[4:5]=SPEED(km/h×0.01)**, BYTE[6]=Trip(km) |
| **0x17C** | RPM | BYTE[0]=Throttle, BYTE[2:3]=RPM(uint16) |
| **0x309** | CAR_SPEED (alt) | BYTE[0]=Speed km/h (raw) — alternate source, not primary |
| **0x324** | TEMPERATURES | BYTE[0]=Coolant(-40), BYTE[1]=Intake(-40) |
| **0x1A6** | SCM_BUTTONS | BYTE[3]=Fuel(%), BYTE[5]=Fuel(map), BYTE[1]=Cruise |
| **0x13C** | GAS_PEDAL | BYTE[0]=Pedal raw 0-255 |
| **0x191** | GEAR | BYTE[0] & 0x07 = Gear position |
| **0x1A4** | BRAKE | BYTE[0]=Brake % |
| **0x18E** | ACCEL | BYTE[0/1]=Lateral/Longitudinal G (int8×0.01) |
| **0x294** | SCM_FEEDBACK | BYTE[0]=Turn signals(WIPERS), **BYTE[1]=DIMMER**(0x00=dim 0x01..0x15=mid 0x96=MAX), BYTE[3:5]=ODOMETER(uint24, km) |
| **0x255** | WHEEL_SPEEDS | 4× uint16 = individual wheel speeds |

### Self-Diagnosis Mode
1. Hold SEL/RESET button on cluster
2. Turn parking lights ON
3. Ignition ON (within 5s toggle parking OFF→ON→OFF)
4. Release SEL/RESET, press 3 times
5. Gauge test: needle sweep + LCD segments + **CAN errors** (Err1=F-CAN, Err2=B-CAN)

---

## Project Files

```
D:\Gemini\cyd_can_sniffer\
├── cyd_can_multitool/                 ← v2.0 current
│   └── cyd_can_multitool.ino          ← main sketch (~1400 lines)
├── cyd_can_sniffer.ino                ← v1.2 stable (legacy)
├── cyd_can_sniffer_v1.2/              ← v1.2 backup
├── README.md                          ← this file
├── описание проекта.md                ← Russian description
├── HISTORY.md                         ← full project history
├── Honda_Toyota_CAN_ID_Map.xlsx       ← CAN ID knowledge base
├── CAN_Projects_List.md               ← 26 found projects
├── lib/                               ← Required libraries
│   ├── MCP_CAN/                       ← CAN controller v2.x
│   ├── TFT_eSPI-CYD/                  ← CYD display config
│   └── README.md
├── *.py                               ← Fix/debug scripts (*_fix_*.py)
└── Honda_Accord8_Service_Manual/      ← Full service manual (65 pages)
```

---

## Diff-Based Redraw

All live-update modes use **previous value tracking** to avoid unnecessary redraws:

| Mode | Tracked values | Trigger |
|------|---------------|---------|
| Speedometer | `prevSpeedoVal`, `prevRpmVal` | Any change in value |
| Dimmer | `prevBrightness` | Any change in value |
| Monitor | `prevMonitorBytes[8]`, `monChanged` | Any byte change |
| Sensors | `prevCoolantTemp`, `prevFuelLevel`, `prevThrottlePos`, `prevBatteryVolt` | Any change in value |

Only `valueUpdateNeeded = true` triggers redraw — no timer-based flicker.

---

## Font System

| Purpose | Font | Sizes |
|---------|------|-------|
| Speedo/RPM digits | **Font7** (7-segment RLE) | size 2 (single), size 1 (BOTH) |
| Monitor hex bytes | **Font7** | size 2 |
| All text/labels/buttons | **FreeSansBold** (GFXFF) | 9pt, 12pt |
| Headers | **FreeSansBold** | 12pt |

---

## Safety

⚠️ **CAN bus connects to ABS, SRS (airbags), and engine ECU.**

| Mode | Transmits? | Risk |
|------|-----------|------|
| Scanner, Monitor, Speedo, Sensors | ❌ No | Safe — receive only |
| ECU Scan, OBD2 (future) | ✅ Yes | Use with caution |
| SLCAN, Emulator (future) | ✅ Yes | Expert use only |

**Disconnect module before plugging/unplugging OBD2 connector.**

---

| v2.0-patch2 | 27.05 | Font7 + FreeSansBold overhaul + sensor picker |
**Current version:** 2.0
## Version History

| Tag | Date | Description |
|-----|------|-------------|
| v1.0 | 25.05 | First scanner + LIST mode |
| v1.1 | 25.05 | Sort by changes, highlight in scan |
| v1.2 | 26.05 | MONITOR bar layout, anti-flicker (50ms throttle) |
| **v2.0-alpha** | **27.05** | CAN-Multitool: menu + 6 modes + decoder (11 IDs) |
| v2.0-patch1 | 27.05 | 4 real-hardware bugs fixed + diff-based redraw |
| v2.0-patch2 | 27.05 | Font7 + FreeSansBold overhaul + sensor picker |
| **v2.2** | **29-30.05** | **DIMMER mode + menu 2×3 + 0x96=MAX dimmer + speedo anti-flicker + SCR_W fill fix** |

---

**Authors:** [Kiro (AI)](https://github.com/kiro) + [Sergey / Lexx79](https://github.com/Lexx79)  
**Started:** May 25th, 2026  
**Current version:** 2.2  
**Test vehicle:** Honda Accord 8 (2008–2012) P-CAN 500 kbps  
**GitHub:** [Lexx79/cyd-can-sniffer](https://github.com/Lexx79/cyd-can-sniffer)
