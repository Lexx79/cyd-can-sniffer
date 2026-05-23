# CAN ID Hunter — ESP32 CYD

**Find your car's dashboard illumination CAN ID.**

[![ESP32](https://img.shields.io/badge/ESP32-2432S028-blue)](https://github.com/espressif/arduino-esp32)
[![Version](https://img.shields.io/badge/version-1.0-success)](.)

Firmware for **ESP32-2432S028 (Cheap Yellow Display)** + **MCP2515+TJA1050** CAN module.
Scans CAN bus for 30 seconds, finds the ID that changes when you rotate the dimmer wheel.

> Many Honda (Accord 8, 2008-2012) illumination IDs are **not documented** in openpilot/opendbc.
> This tool finds them in 30 seconds.

---

## How It Works

1. **START SCAN** → listens to CAN bus for 30s, counts every ID and data changes
2. **LIST** → IDs sorted by "change count" (most active → top). The illumination ID jumps to the top.
3. Tap an ID → **MONITOR** → first byte displayed as brightness indicator
4. Wrong ID? Tap BACK, pick the next one.

---

## Hardware

This is **CAN sniffer only** — no LED strip, no MOSFET.

### Wiring MCP2515 → CYD

The CYD has **no 2×8 header**. SPI pins (18, 19, 23) are only available at the ESP32 chip legs or at the SD card slot contacts. **Some soldering is required.**

#### Option A — Solder to ESP32 legs (recommended, 5V safe)

| MCP2515 | CYD signal | CYD location | Solder? |
|---------|-----------|-------------|---------|
| VCC (5V) | 5V | Pad **S3** (back of PCB) | ⚠️ Yes |
| GND | GND | Pad **S1** (back of PCB) | ⚠️ Yes |
| SCK | GPIO 18 | ESP32 leg | ⚠️ Yes |
| MOSI | GPIO 23 | ESP32 leg | ⚠️ Yes |
| MISO | GPIO 19 | ESP32 leg | ⚠️ Yes |
| **CS** | **GPIO 22** | **P3 connector, pin 3** | **No — just a dupont wire** |
| CAN_H | — | MCP2515 terminal | No |
| CAN_L | — | MCP2515 terminal | No |

**5 solder joints + one wire into P3.**
S3 (5V) and S1 (GND) are nice solder pads, easy to use.

#### Option B — Solder to SD card slot contacts

| MCP2515 | SD slot contact | CYD signal | Solder? |
|---------|----------------|-----------|---------|
| VCC | SD pin 4 (VDD) | 3.3V | ⚠️ Yes |
| GND | SD pin 3/6 (VSS) | GND | ⚠️ Yes |
| SCK | SD pin 5 (CLK) | GPIO 18 | ⚠️ Yes |
| MOSI | SD pin 2 (CMD/DI) | GPIO 23 | ⚠️ Yes |
| MISO | SD pin 7 (DO) | GPIO 19 | ⚠️ Yes |
| **CS** | **P3 pin 3** | **GPIO 22** | **No — dupont into P3** |

**6 solder joints to SD card contacts.** But VCC from SD is 3.3V — MCP2515 needs 5V to drive TJA1050 transceiver. Use Option A for 5V.

### CYD layout

```
                 CYD (rear view)
    ┌──────────────────────────────────────┐
    │                                      │
    │   ESP32 legs:                        │
    │     18(SCK) 19(MISO) 23(MOSI)       │
    │                                      │
    │   Solder pads:                       │
    │   S1(GND)   S3(5V)                   │
    │                                      │
    │   P3 connector:                      │
    │   ┌──────────────────┐               │
    │   │ 1:GND  2:35      │               │
    │   │ 3:GPIO22 ← CS    │ ← 1 wire     │
    │   │ 4:GPIO21         │               │
    │   └──────────────────┘               │
    └──────────────────────────────────────┘
```

### OBD2 → MCP2515
- CAN_H → pin 6
- CAN_L → pin 14
- GND → pin 4

---

## Pin Table

| Pin | Project Use | Location | Solder? |
|-----|-------------|----------|---------|
| 22 | **CAN_CS** | **P3 (pin 3)** | **No — just plug in** |
| 18 | SPI SCK | ESP32 leg | ⚠️ Yes |
| 19 | SPI MISO | ESP32 leg | ⚠️ Yes |
| 23 | SPI MOSI | ESP32 leg | ⚠️ Yes |
| 4 | TFT_RST | DO NOT TOUCH | — |
| 5 | SD_CS | Free | — |
| 26 | Free | ESP32 leg | — |
| — | 5V | S3 pad | ⚠️ Yes |
| — | GND | S1 pad | ⚠️ Yes |

---

## Known Illumination IDs (from opendbc)

Only exterior lights are confirmed — **no Honda dashboard dimming ID is publicly documented**:

| ID (hex) | Module | Signals |
|----------|--------|---------|
| 0x374 | STALK_STATUS | HEADLIGHTS_ON, AUTO_HEADLIGHTS |
| 0x37B | STALK_STATUS_2 | LOW_BEAMS, HIGH_BEAMS, PARK_LIGHTS |

Toyota has `METER_SLIDER_BRIGHTNESS_PCT` at 0x610 — Honda doesn't.

---

## Requirements

| Component | Status |
|-----------|--------|
| ESP32-2432S028 (CYD) | ✅ Have |
| MCP2515 + TJA1050 | 🚚 Ordered (Ozon) |

**Libraries (Arduino IDE):**
- TFT_eSPI-CYD (or TFT_eSPI)
- MCP_CAN
- Board: ESP32 Dev Module

---

## Safety

⚠️ **The CAN bus connects to ABS, SRS (airbags), and engine ECU.**
- **RECEIVE ONLY** — the sketch never transmits
- Disconnect before plugging/unplugging hardware
- Test on a bench first

---

## File Structure

```
D:\Gemini\cyd_can_sniffer\
├── cyd_can_sniffer.ino          ← current dev sketch
├── README.md                    ← this file (English)
├── описание проекта.md          ← project description (Russian)
├── .gitignore
└── cyd_can_sniffer_v1.0\        ← v1.0 release
    ├── cyd_can_sniffer_v1.0.ino
    ├── README_v1.0.md
    └── .gitignore
```

---

## License

MIT

**Author:** Kiro (⚡) — May 2026
