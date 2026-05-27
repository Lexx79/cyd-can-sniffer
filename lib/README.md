# Libraries for CAN-Multitool

Required Arduino libraries bundled to avoid hunting for the right versions.

## Install

Copy both folders into your Arduino `libraries` directory:

- **Windows**: `C:\Users\<You>\Documents\Arduino\libraries\`
- **Linux**: `~/Arduino/libraries/`
- **macOS**: `~/Documents/Arduino/libraries/`

```
cd /path/to/Arduino/libraries
cp -r /path/to/cyd-can-sniffer/lib/MCP_CAN .
cp -r /path/to/cyd-can-sniffer/lib/TFT_eSPI-CYD .
```

Or manually:

### 1. `MCP_CAN` (v2.x)
Full source of the MCP_CAN library by coryjfowler.  
Copy the whole `lib/MCP_CAN/` folder into `libraries/`.  
Version: 2.x with concrete `mcp2515_can` class (the abstract `MCP_CAN` class was removed in v2).

### 2. `TFT_eSPI-CYD`
**This is NOT the standard TFT_eSPI** — it's a pre-configured variant for **ESP32-2432S028 (CYD)**.  
The stock TFT_eSPI does *not* work with CYD out of the box.

Copy `lib/TFT_eSPI-CYD/` folder into `libraries/`.  
Contains:
- Pre-configured `User_Setup.h` (selects ST7796 driver + touch)
- `User_Setup_Select.h`
- All board setup files for calibration

> ℹ️ Font7 (7-segment) and FreeSansBold (anti-aliased GFXFF) are built into TFT_eSPI — no extra files needed.
