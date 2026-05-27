# ⚡ CAN-Multitool — Onboard Diagnostic Center on CYD

**Version: 2.0 (in development)**

[![ESP32](https://img.shields.io/badge/ESP32-2432S028-blue)](https://github.com/espressif/arduino-esp32)
[![Version](https://img.shields.io/badge/version-2.0--dev-orange)](.)

From a simple CAN ID scanner to a full **onboard diagnostic center**.
One cheap ESP32 display, infinite possibilities.

---

## 🌟 The Vision

**CAN-Multitool** turns a $10 CYD display into a professional-grade CAN bus tool:

- **🔍 Scanner** — Find unknown CAN IDs in 30 seconds
- **📊 Monitor** — Decode raw bytes into RPM, speed, temperature, fuel level
- **🏎️ Dashboard** — Speedometer, tachometer, gauges
- **🔎 Block finder** — Scan for every ECU module on the bus (UDS)
- **📡 OBD2** — Read engine data without knowing CAN IDs
- **📝 Logger** — Dump traffic to SD card
- **🧪 Test mode** — Manual ID probe for reverse engineering

And more. All on a $10 ESP32 display with a $3 CAN module.

---

## 🧩 Mode Overview

| # | Mode | Status | Description |
|---|------|--------|-------------|
| 1 | 🔍 **CAN Scanner** | ✅ v1.2 stable | 30s scan, sort by changes, ID monitor |
| 2 | 📊 **Value Monitor** | 🔧 Active | Decode bytes: RPM, speed, temp, fuel |
| 3 | 🏎️ **Speedo/Tacho** | ⏳ Planned | Giant digits, km/h + RPM |
| 4 | 🔬 **Engine Sensors** | ⏳ Planned | 6-grid live sensors |
| 5 | 🔎 **Block Finder** | ⏳ Planned | UDS module discovery |
| 6 | 📝 **CAN Logger** | ⏳ Planned | CSV dump to SD card |
| 7 | 📡 **OBD2 Scanner** | ⏳ Planned | Standard OBD2 PID reader |
| 8 | 🧪 **Manual Probe** | ⏳ Planned | Enter any ID, watch bytes |
| 9 | 🌐 **SLCAN Bridge** | ⏳ Planned | SavvyCAN on PC via USB |
| 10 | 🔄 **CAN Emulator** | ⏳ Planned | Generate test packets |

---

## How It Started

This project began with a practical problem: finding the **dashboard illumination CAN ID** on a **Honda Accord 8 (2008-2012)**.

The dimmer wheel brightness control ID is well known for Toyota (`METER_SLIDER_BRIGHTNESS_PCT` at 0x610) but **completely undocumented for Honda** — not in openpilot/opendbc, not in community forums.

The CAN ID scanner was born to find it. And then... it grew.

---

## Hardware

### Current (v1.2 / v2.0-dev)
| Component | Purpose | Status |
|-----------|---------|--------|
| ESP32-2432S028 (CYD) | MCU + 320×240 touch display | ✅ Have |
| MCP2515 + TJA1050 | CAN controller (SPI, CS=22) | ✅ Have |

### Wiring MCP2515 → CYD

**Soldering required** — 5 points on ESP32 legs + 1 wire into P3 connector.

| MCP2515 | CYD signal | Location | Solder? |
|---------|-----------|----------|---------|
| VCC (5V) | 5V | Pad **S3** (back) | ⚠️ Yes |
| GND | GND | Pad **S1** (back) | ⚠️ Yes |
| SCK | GPIO 18 | ESP32 leg | ⚠️ Yes |
| MOSI | GPIO 23 | ESP32 leg | ⚠️ Yes |
| MISO | GPIO 19 | ESP32 leg | ⚠️ Yes |
| **CS** | **GPIO 22** | **P3, pin 3** | ❌ Just wire |
| CAN_H | — | MCP2515 → OBD2 pin 6 | ❌ |
| CAN_L | — | MCP2515 → OBD2 pin 14 | ❌ |

### Future Hardware Upgrades
1. **Second MCP2515 + TJA1050** for B-CAN (125 kbit) — dual bus monitoring
2. **TWAI native** — built-in CAN controller on ESP32-S3/C6 (no MCP2515 needed)
3. **RejsaCAN v6.x** — ESP32-C6, dual CAN, 12V power, auto shutdown

---

## Architecture

```
┌──────────────────────────────────────┐
│          CAN-Multitool                │
│                                       │
│  ┌────────┐ ┌──────────┐ ┌─────────┐ │
│  │  MENU  │ │ Modes    │ │ Settings│ │
│  │        │ │ 1-10     │ │         │ │
│  └────────┘ └──────────┘ └─────────┘ │
│                                       │
│  ┌──────────────────────────────────┐ │
│  │  CAN Core (FIFO, SPI, callbacks) │ │
│  └──────────────────────────────────┘ │
│                                       │
│  ┌──────────────────────────────────┐ │
│  │  Knowledge Base (ID table, DBC)  │ │
│  └──────────────────────────────────┘ │
└──────────────────────────────────────┘
```

---

## Resources

- **Knowledge base:** `Honda_Toyota_CAN_ID_Map.xlsx` — 55+ known CAN IDs with byte parsing
- **HondaCAN (Ldalvik):** 22 parsed P-CAN IDs from Accord 2016 LX
- **Opendbc (commaai):** Honda DBC files in `D:\Gemini\opendbc_honda\`
- **RejsaCAN-ESP32 (MagnusThome):** Hardware reference — dual CAN, TWAI, 12V power
- **Service manual:** haccord.org
- **CAN projects list:** `CAN_Projects_List.md` — 26 projects

---

## Safety

⚠️ **The CAN bus connects to ABS, SRS (airbags), and engine ECU.**
- Mode 1 (Scanner) and Mode 2 (Monitor) are **receive-only** — safe
- Modes 5, 7, 9 require transmission — **use with caution**
- Disconnect before plugging/unplugging hardware

---

## File Structure

```
D:\Gemini\cyd_can_sniffer\
├── cyd_can_sniffer.ino          ← v1.2 stable (legacy)
├── cyd_can_multitool.ino        ← v2.0 CAN-Multitool (active dev!)
├── README.md                    ← this file (English)
├── описание проекта.md          ← project description (Russian)
├── HISTORY.md                   ← full project history
├── Honda_Toyota_CAN_ID_Map.xlsx ← CAN ID database
├── CAN_Projects_List.md         ← found projects reference
├── RejsaCAN-ESP32/              ← RejsaCAN clone (reference)
├── cyd_can_sniffer_v1.0/
├── cyd_can_sniffer_v1.1/
├── cyd_can_sniffer_v1.2/
└── ...
```

---

## Roadmap

### Phase 1 — Foundation ✅
- v1.0 Basic scanner + LIST
- v1.1 Sort by changes
- v1.2 MONITOR layout, anti-flicker
- Excel with HondaCAN ID table
- GitHub repo

### Phase 2 — Multitool 🔧 *[NOW]*
- Main menu with touch buttons
- Scanner mode (ported from v1.2)
- Value monitor (byte decoder)
- Speedometer/Tachometer
- Rename to CAN-Multitool

### Phase 3 — Diagnostics
- Block finder (UDS)
- OBD2 scanner (PID)
- Custom car profiles
- Save/load settings

### Phase 4 — Pro
- SD card logger
- SLCAN (SavvyCAN)
- B-CAN support (2nd MCP2515)
- Real-time byte graphs

### Phase 5 — Release
- RejsaCAN v6.x dual CAN support
- Native TWAI (no MCP2515)
- LVGL graphical dashboard
- BLE phone output

---

**Authors:** Kiro (⚡) + Sergey (@Lexxabk)  
**Started:** May 25th, 2026  
**Current version:** 2.0-dev  
**Test vehicle:** Honda Accord 8 (2008-2012)
