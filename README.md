# CAN Brightness Hunter — ESP32 CYD

**Find your car's dashboard illumination CAN ID, then drive an LED strip.**

[![ESP32](https://img.shields.io/badge/ESP32-2432S028-blue)](https://github.com/espressif/arduino-esp32)
[![Version](https://img.shields.io/badge/version-1.0-success)](.)

Firmware for **ESP32-2432S028 (Cheap Yellow Display)** + **MCP2515+TJA1050** CAN module.
Designed to sniff CAN bus for the elusive **dashboard/pedal illumination** CAN ID — the one that
controls interior brightness when you rotate the dimmer wheel.

> Many Honda (Accord 8, 2008-2012) illumination IDs are **not documented** in openpilot/opendbc.
> This tool finds them in 30 seconds.

---

## How It Works

1. **START SCAN** → listens to CAN bus for 30s, counts every ID and how many times its data changed
2. **LIST** → IDs sorted by "change count" (most active → top). The real illumination ID will
   jump to the top because you were rotating the wheel.
3. Tap an ID → **MONITOR** → first byte drives brightness indicator on screen AND an external LED strip
4. Wrong ID? Tap BACK, pick the next one.

Screenshots
```
┌─────────────────────┐  ┌─────────────────────┐  ┌─────────────────────┐
│     CAN HUNTER      │  │     SCANNING...     │  │FOUND ID by changes1/4│
│ ● MCP2515: OK 500K  │  │ Listening...  23s   │  │┌───────────────────┐│
│                     │  │ ○ Rotate wheel NOW  │  ││0x160 rx:214 chg:52││
│ ┌─────────────────┐ │  │ ████████░░░░░░░░░░░ │  ││0x374 rx:188 chg:3 ││
│ │ 1 Turn ign. ON  │ │  │                     │  ││0x130 rx:92  chg:1 ││
│ │ 2 START SCAN    │ │  │     ┌────────┐      │  ││...                ││
│ │ 3 Rotate wheel  │ │  │     │  STOP  │      │  │└───────────────────┘│
│ │ 4 Tap ID        │ │  │     └────────┘      │  │ [<] [>] [SCAN AGAIN]│
│ └─────────────────┘ │  └─────────────────────┘  └─────────────────────┘
│     ┌──────────┐    │
│     │START SCAN│    │
│     └──────────┘    │
└─────────────────────┘
```

---

## Hardware

### Wiring

| MCP2515 | ESP32 CYD | OBD2 Pin |
|---------|-----------|----------|
| VCC     | 5V        | 16 (12V via 5V reg) |
| GND     | GND       | 4 / 5 |
| SCK     | GPIO 18   | — |
| MOSI    | GPIO 23   | — |
| MISO    | GPIO 19   | — |
| CS      | GPIO 32   | — |
| CAN_H   | —         | 6 |
| CAN_L   | —         | 14 |

### LED strip

```
GPIO26 → 100Ω ── Gate IRLZ44N ── Drain ── LED strip (-)
                  │                       
                10kΩ               LED strip (+) → +12V (fused 1-2A)
                  │                       
                 GND              Supply GND ── Source
```

**MOSFET: IRLZ44N** (logic-level, fully on at 3.3V)

---

## Pin Notes

All pins are free to use except **GPIO 4** (TFT_RST/reset).

| Pin | Project Use | Can Change? |
|-----|-------------|-------------|
| 18  | SPI SCK | No (display + CAN) |
| 19  | SPI MISO | No |
| 23  | SPI MOSI | No |
| 4   | TFT_RST | **DO NOT TOUCH** |
| 5   | SD_CS | Yes (free) |
| 22  | CAN_CS alt | Yes |
| 26  | LED_PWM | Yes |
| 32  | CAN_CS | Yes |

---

## Known Illumination IDs (from opendbc)

Only exterior lights are confirmed in opendbc — **no Honda dashboard dimming ID is publicly documented**:

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
| IRLZ44N | ❌ Needed |
| LED strip 12V | ❌ Needed |

**Libraries (Arduino IDE):**
- TFT_eSPI-CYD (or TFT_eSPI)
- MCP_CAN
- Board: ESP32 Dev Module

---

## Safety

⚠️ **The CAN bus connects to ABS, SRS (airbags), and engine ECU.**
- **RECEIVE ONLY** — the sketch never transmits
- Disconnect before plugging/unplugging hardware
- Fuse the LED strip at 1-2A
- Test on a bench first

---

## Future Ideas

- [ ] Save found ID to EEPROM
- [ ] SD card CAN logging (GPIO5)
- [ ] Dual-channel LED (passenger + driver)
- [ ] Animation modes (turn signal sync, fade in/out)
- [ ] Auto-detect brightness byte position

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

MIT — feel free to use, modify, share.

**Author:** Kiro (⚡) — May 2026
