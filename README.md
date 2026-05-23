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

### Connecting MCP2515 (no soldering!)

| MCP2515 | ESP32 CYD | Connector |
|---------|-----------|-----------|
| VCC     | 5V        | 2×8 header (back) |
| GND     | GND       | 2×8 header (back) |
| SCK     | GPIO 18   | 2×8 header (back) |
| MOSI    | GPIO 23   | 2×8 header (back) |
| MISO    | GPIO 19   | 2×8 header (back) |
| **CS**  | **GPIO 22** | **P3, pin 3** |

### OBD2 → MCP2515
- CAN_H → pin 6
- CAN_L → pin 14
- GND → pin 4 / 5
- +12V → not connected (MCP2515 powered from CYD 5V)

### CYD connectors used

```
       ┌─────────────────────────────┐
       │     2×8 header (back)       │
       │  5V GND MISO(19) SCK(18)    │
       │         MOSI(23)            │
       └─────────────────────────────┘
               │
               │
       P3: ┌───┴──────────────┐
           │ 1: GND   2: 35   │
           │ 3: GPIO22 ← CS   │ ← one wire!
           │ 4: 21            │
           └──────────────────┘
```

**Result: only one extra wire needed** — from MCP2515 CS to P3 pin 3 (GPIO22). Everything else through the standard 2×8 SPI header.

---

## Pin Table

| Pin | Project Use | Connector |
|-----|-------------|-----------|
| 22  | CAN_CS | P3 (pin 3) |
| 18  | SPI SCK | 2×8 header |
| 19  | SPI MISO | 2×8 header |
| 23  | SPI MOSI | 2×8 header |
| 4   | TFT_RST | DO NOT TOUCH |
| 5   | SD_CS | Free |
| 26  | Free | Back pin |

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
├── README.md                    ← this file
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
