# CAN-Multitool — Onboard Diagnostic Center on CYD

**Version: 2.0-alpha**

[![ESP32](https://img.shields.io/badge/ESP32-2432S028-blue)]()
[![Version](https://img.shields.io/badge/version-2.0--alpha-orange)](.)
[![License](https://img.shields.io/badge/license-MIT-green)](.)

From a simple CAN ID scanner to a full **onboard diagnostic center**.
One cheap ESP32 display, infinite possibilities.

---

## Quick Start

| Step | What to do |
|------|------------|
| 1 | Wire MCP2515+TJA1050 → CYD (SCK=18, MOSI=23, MISO=19, CS=22, 5V=S3, GND=S1) |
| 2 | Connect CAN_H → OBD2 pin 6, CAN_L → OBD2 pin 14 |
| 3 | Open `cyd_can_multitool/cyd_can_multitool.ino` in Arduino IDE |
| 4 | Select board: **ESP32-2432S028** (CYD) |
| 5 | Flash and touch the screen |

**Libraries required:**
- `TFT_eSPI` (CYD variant)
- `MCP_CAN` v2.x (by coryjfowler)

---

## Hardware

### Current Setup
| Component | Purpose |
|-----------|---------|
| ESP32-2432S028 (CYD) | MCU + 320×240 color touch display |
| MCP2515 + TJA1050 | CAN controller via SPI (CS=22) |

### Wiring (soldered)

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

### Future Upgrades
- **Second MCP2515** for B-CAN (125 kbps) monitoring
- **RejsaCAN v6.x** — ESP32-C6, dual native TWAI, 12V OBD2 power
- **TWAI direct** — built-in CAN on ESP32-S3/C6, no external controller

---

## Mode Overview

### 1. CAN ID SCANNER (✅ implemented)
Scans the bus for 30 seconds, collects every CAN ID seen, counts how often each ID changes. Results are sorted by change frequency (most active first).

| Feature | Detail |
|---------|--------|
| Duration | 30 seconds |
| Progress bar | Visual countdown with pulse indicator |
| Sorting | By number of data changes (descending) |
| Output | List of IDs with change count |
| Tap | Any ID opens Value Monitor for that ID |

### 2. VALUE MONITOR (✅ implemented)
Watch decoded values from any CAN ID on the bus. Two sub-modes:

**RAW mode** — bar graph showing first byte value (0–255) + percentage + raw hex bytes.
- Left column bar (60×170 px), large percentage, raw hex below
- Auto-updates from any ID, 50ms debounce to prevent flicker

**DECODE mode** — human-readable values parsed from known Honda CAN IDs.
Currently 11 decoders built-in:

| CAN ID | Decoded Values | Formula |
|--------|---------------|---------|
| 0x17C | RPM, Throttle % | RPM = (data[2]<<8\|data[3]), Throttle = data[0] |
| 0x309 | Speed (km/h) | data[0] |
| 0x324 | Coolant °C, Intake °C | data[0]-40, data[1]-40 |
| 0x1A6 | Fuel %, Cruise | map(data[5],0,255,0,100) |
| 0x191 | Gear (P/R/N/D/2/1/L/S) | data[0] & 0x07 |
| 0x1A4 | Brake % | map(data[0],0,255,0,100) |
| 0x158 | Speed (km/h, 0.01 res) | (data[1]<<8\|data[2]) * 0.01 |
| 0x13C | Throttle % | data[0] * 100 / 255 |
| 0x18E | Lateral/Longitudinal G | int8_t(data[n]) * 0.01 |
| 0x294 | Turn signals, Odometer (km) | bits + 24-bit odometer |
| 0x255 | Wheel speeds (4× km/h) | 16-bit per wheel |

The decoder table is extensible — add any new ID with parsing formula.

### 3. SPEEDOMETER (✅ implemented)
Full-screen digital speedometer and tachometer. Three sub-modes:

| Mode | Display | Text Size |
|------|---------|-----------|
| SPD | Speed (km/h) only | 7 (huge) |
| RPM | RPM only | 7 (huge) |
| BOTH | Speed + RPM stacked | 5 each |

Footer buttons: `[SPD] [BTH] [RPM]` (left-aligned) + `[MENU]` (red, right-aligned).
Active mode is highlighted with grey background.

### 4. ENGINE SENSORS (✅ implemented)
6-cell grid showing live engine sensor values:

| Cell | Sensor | Unit |
|------|--------|------|
| 1 | Speed | km/h |
| 2 | RPM | — |
| 3 | Coolant Temp | °C |
| 4 | Fuel Level | % |
| 5 | Throttle | % |
| 6 | Battery | V |

Values populate automatically from the CAN decoder when matching IDs appear on the bus.

### 5. ECU SCAN (⏳ planned)
UDS-based module discovery. Sends diagnostic requests across the CAN ID range to find every ECU on the bus. Displays a module map.

### 6. CAN LOGGER (⏳ planned)
Records all CAN traffic to SD card in CSV format.
Columns: timestamp, CAN ID, DLC, data[0–7].

### 7. OBD2 SCANNER (⏳ planned)
Standard OBD2 PID requests via 0x7DF → 0x7E8.
Reads speed, RPM, coolant temp, engine load, throttle position without knowing CAN IDs.

### 8. PROBE ID (⏳ planned)
Manual CAN ID entry. Watch live bytes from any ID in real-time.

### 9. SLCAN BRIDGE (⏳ planned)
USB-CAN adapter mode. ESP32 acts as SLCAN interface for SavvyCAN or other PC tools.

### 10. CAN EMULATOR (⏳ planned)
Generate fake CAN packets for testing and development.

---

## Menu System

- **Main menu**: 2×2 grid, 8 items on 2 pages
- **Status indicators**: green `+` = implemented, red `-` = planned
- **Navigation**: `<` / `>` footer buttons for page switching
- **Footer bar**: 30px bottom bar with touch buttons, consistent across all modes

```
┌──────────────────────────────────┐
│  ⚫ CAN-MULTITOOL        v2.0    │ ← Header with CAN status dot
├──────────────────────────────────┤
│  [+ SCAN ID]  [+ MONITOR]       │
│  [+ SPEEDO]   [+ SENSORS]       │ ← 2×2 grid
│  [- ECU SCAN] [- LOGGER]        │
│  [- OBD2]     [- PROBE ID]      │
├──────────────────────────────────┤
│  [<]         MENU         [>]    │ ← Footer bar
└──────────────────────────────────┘
```

---

## Project Files

```
D:\Gemini\cyd_can_sniffer\
├── cyd_can_sniffer.ino                ← v1.2 stable (legacy)
├── cyd_can_multitool/                 ← v2.0 current
│   └── cyd_can_multitool.ino          ← main sketch (1147 lines)
├── README.md                          ← this file
├── описание проекта.md                ← Russian description
├── HISTORY.md                         ← full project history
├── Honda_Toyota_CAN_ID_Map.xlsx       ← CAN ID knowledge base
├── CAN_Projects_List.md               ← 26 found projects
├── RejsaCAN-ESP32/                    ← RejsaCAN reference clone
├── cyd_can_sniffer_v1.0/
├── cyd_can_sniffer_v1.1/
├── cyd_can_sniffer_v1.2/
└── ...
```

---

## Knowledge Base

### Honda CAN Architecture (Accord 8, 2008–2012)

| Bus | Type | Speed | Function |
|-----|------|-------|----------|
| **F-CAN** (P-CAN) | High-Speed CAN | **500 kbps** | Powertrain: engine, transmission, ABS, EPS |
| **B-CAN** | High-Speed CAN | **125 kbps** | Body: lights, wipers, locks, dash dimmer |

**Gateway**: Instrument cluster bridges selected signals:
- **F→B**: Vehicle speed, coolant temp
- **B→F**: Ignition key, seatbelt, steering wheel buttons (as SCM_BUTTONS 0x296)
- **NOT bridged**: Dashlight dimmer (0x341), turn signals, wipers, climate, door locks

### Source References
- **HondaCAN (Ldalvik)**: 22 parsed F-CAN IDs from Accord 2016 LX
- **Opendbc (commaai)**: 31 Honda DBC files (F-CAN only)
- **Community**: B-CAN IDs from Honda forums and reverse engineering
- **Service manual**: haccord.org

---

## Safety

⚠️ **CAN bus connects to ABS, SRS (airbags), and engine ECU.**

| Mode | Transmits? | Risk |
|------|-----------|------|
| Scanner, Monitor, Speedo | ❌ No | Safe — receive only |
| Sensors | ❌ No | Safe — receive only |
| ECU Scan, OBD2, Probe | ✅ Yes | Use with caution |
| SLCAN, Emulator | ✅ Yes | Expert use only |

**Physical safety:**
- Disconnect module before plugging/unplugging OBD2 connector
- Ensure good CAN_H/CAN_L connection (twisted pair)
- Do not short CAN lines to power or ground

---

## Roadmap

### Phase 1 — Foundation ✅
| Step | Detail |
|------|--------|
| v1.0 | Basic scanner + ID list |
| v1.1 | Sort by change frequency |
| v1.2 | Monitor bar, anti-flicker, GitHub release |
| Research | HondaCAN parsing, opendbc, Excel database |

### Phase 2 — Multitool 🔧 **[NOW]**
| Step | Status |
|------|--------|
| Main menu with touch navigation | ✅ |
| Scanner mode (ported) | ✅ |
| Value Monitor RAW + DECODE | ✅ |
| Speedometer (3 modes) | ✅ |
| Engine Sensors grid | ✅ |
| Python decoder script | ⏳ Next |

### Phase 3 — Diagnostics ⏳
- UDS block finder
- OBD2 PID scanner
- Car profile system
- Settings save/load

### Phase 4 — Pro ⏳
- SD card logger
- SLCAN bridge to SavvyCAN
- B-CAN monitoring (2nd MCP2515)
- Live byte graphs

### Phase 5 — Release ⏳
- RejsaCAN v6.x dual CAN support
- Native TWAI (no MCP2515)
- LVGL dashboard
- BLE output

---

## Version History

| Tag | Date | Description |
|-----|------|-------------|
| v1.0 | 25.05 | First scanner + LIST mode |
| v1.1 | 25.05 | Sort by changes, highlight in scan |
| v1.2 | 26.05 | MONITOR bar layout, anti-flicker (50ms throttle) |
| **v2.0-alpha** | **27.05** | **CAN-Multitool: menu + 6 modes + decoder (11 IDs)** |

---

**Authors:** [Kiro](https://github.com/kiro) + [Sergey / Lexx79](https://github.com/Lexx79)  
**Started:** May 25th, 2026  
**Current version:** 2.0-alpha  
**Test vehicle:** Honda Accord 8 (2008–2012)  
**GitHub:** [Lexx79/cyd-can-sniffer](https://github.com/Lexx79/cyd-can-sniffer)
