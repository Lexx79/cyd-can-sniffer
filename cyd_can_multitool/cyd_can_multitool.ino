// ═══════════════════════════════════════════════════════════════
//  CAN-Multitool - onboard diagnostic center on CYD
//  Version 2.2
//  Authors: Kiro (⚡) + Sergey (@Lexxabk)
//  Car: Honda Accord 8 (2008-2012)
//  Hardware: ESP32-2432S028 (CYD) + MCP2515+TJA1050
//
//  Modes (ready 1-5):
//    1. SCAN ID     4. SENSORS      7. LOGGER
//    2. MONITOR     5. DIMMER       8. OBD2
//    3. SPEEDO      6. ECU SCAN
// ═══════════════════════════════════════════════════════════════

#include <SPI.h>
#include <TFT_eSPI.h>
#include <mcp_can.h>
#include <mcp2515_can.h>

// -- Smooth fonts --
// FreeSansBold fonts are auto-included from TFT_eSPI (Fonts/GFXFF/)

// -- TFT --
TFT_eSPI tft;

// -- CAN SPI --
#define CAN_CS        22
mcp2515_can CAN0(CAN_CS);

// -- Screen dimensions --
#define SCR_W         320
#define SCR_H         240
#define HDR_H         26
#define FTR_H         30      // footer bar height

// -- Colors (dark theme) --
#define C_BG          TFT_BLACK
#define C_HEADER      0x4A0A    // dark burgundy
#define C_FOOTER      0x0841    // dark footer bar
#define C_ACCENT      0xFB64    // orange
#define C_DIVIDER     0x4208    // dark grey
#define C_LIST_BG     0x1082    // slightly lighter black
#define C_WHITE       TFT_WHITE
#define C_GREY        0xAD55    // grey
#define C_GREEN       TFT_GREEN
#define C_RED         TFT_RED
#define C_BLUE        TFT_BLUE
#define C_YELLOW      0xFFE0
#define C_BTN_BG      0x1CE7    // blue-grey for buttons

// -- CAN FIFO --
#define FIFO_SIZE     512
struct CanMsg {
  unsigned long id;
  uint8_t len;
  uint8_t data[8];
};
CanMsg fifo[FIFO_SIZE];
volatile int fifoHead = 0;
volatile int fifoTail = 0;

// -- Scan params --
#define MAX_IDS       200
#define SCAN_DURATION 30000UL

struct ScanEntry {
  unsigned long id;
  uint32_t count;
  uint32_t changes;
  uint8_t lastData[8];
  bool firstSeen;
};
ScanEntry scanBuf[MAX_IDS];
int scanCount = 0;
int sortedIdx[MAX_IDS];
int sortedCount = 0;
unsigned long scanStartMs = 0;
unsigned long scanDurationMs = SCAN_DURATION;

// -- Shared sensor values --
int speedoValue = 0;
int rpmValue = 0;
int coolantTemp = 0;
int fuelLevel = 0;
int throttlePos = 0;
int batteryVolt = 0;
int dimmerValue = 0;

// -- Configurable sensor slots (6 cells) --
#define SENSOR_SLOTS 6
unsigned long sensorCanId[SENSOR_SLOTS] = {0x158, 0x17C, 0x324, 0, 0, 0};
int sensorValue[SENSOR_SLOTS];
int prevSensorValue[SENSOR_SLOTS] = {-1, -1, -1, -1, -1, -1};
// which CAN data byte(s) to show: -1 = use dedicated decoder, else raw byte index
int sensorDataByte[SENSOR_SLOTS] = {-1, -1, -1, 0, 0, 0};
int sensorPickSlot = -1; // which slot is being configured in picker mode
const char* sensorLabels[SENSOR_SLOTS] = {"Speed", "RPM", "Coolant", "Fuel", "Throttle", "Battery"};
const char* sensorUnits[SENSOR_SLOTS] = {"km/h", "", " C", " %", " %", " V"};

// -- Previous values for diff-based redraw --
int prevSpeedoVal = -1;
int prevRpmVal = -1;
int prevBrightness = -1;
int prevRawVal = -1;
uint8_t prevMonitorBytes[8] = {0};
bool prevMonitorHasData = false;
int prevCoolantTemp = -128;
int prevFuelLevel = -1;
int prevThrottlePos = -1;
int prevBatteryVolt = -1;

// -- Modes --
enum Mode : uint8_t {
  MODE_MENU = 0,
  MODE_SCANNING,
  MODE_LIST,
  MODE_MONITOR_RAW,
  MODE_SPEEDO,
  MODE_SENSORS,
  MODE_SENSOR_PICK,
  MODE_DIMMER,
  MODE_BLOCKFIND,
  MODE_LOGGER,
  MODE_OBD2,
  MODE_PROBE,
  MODE_SLCAN,
  MODE_EMULATOR
};
Mode mode = MODE_MENU;

// -- Screen state --
bool redrawNeeded = true;
bool valueUpdateNeeded = false;
bool pulseState = false;
unsigned long lastPulseMs = 0;
unsigned long lastYieldMs = 0;
unsigned long lastMonitorUpdate = 0;
#define MONITOR_UPDATE_MS 50

// -- Monitor data --
unsigned long monitorId = 0;
int displayRawVal = 0;
int brightnessVal = 0;
uint8_t monitorBytes[8];
int monitorLen = 0;
bool monitorHasData = false;

// -- Decoder data --
struct DecodedValue {
  const char* label;
  float value;
  const char* unit;
};
DecodedValue decodedVals[4];
int decodedCount = 0;

// -- Navigation --
int listPage = 0;
#define PER_PAGE 4
int menuPage = 0;
#define MENU_PER_PAGE 6

// -- SPI mutex --
volatile bool spiBusy = false;

// -- Menu item descriptor --
struct MenuItem {
  const char* label;
  Mode target;
  uint8_t ready; // 1=green +, 0=red -
};

// Menu items definition
MenuItem menuItems[] = {
  {"[1] SCAN ID",      MODE_SCANNING,    1},
  {"[2] MONITOR",      MODE_MONITOR_RAW, 1},
  {"[3] SPEEDO",       MODE_SPEEDO,      1},
  {"[4] SENSORS",      MODE_SENSORS,     1},
  {"[5] DIMMER",       MODE_DIMMER,      1},
  {"[6] ECU SCAN",     MODE_BLOCKFIND,   0},
  {"[7] LOGGER",       MODE_LOGGER,      0},
  {"[8] OBD2",         MODE_OBD2,        0}
};
const int MENU_ITEM_COUNT = 8;

// ═══════════════════════════════════════════════════════════════
//  HELPER FUNCTIONS
// ═══════════════════════════════════════════════════════════════

void centerText(const char* s, int y, uint16_t color, uint16_t bg, int sz) {
  tft.setTextSize(sz);
  tft.setTextColor(color, bg);
  int w = tft.textWidth(s);
  tft.drawString(s, (SCR_W - w) / 2, y);
}

// --- Footer bar (like v1.2 bottom panel) ---
void drawFooter(const char* btn1, const char* btn2, const char* btn3) {
  tft.fillRect(0, SCR_H - FTR_H, SCR_W, FTR_H, C_FOOTER);
  tft.drawFastHLine(0, SCR_H - FTR_H, SCR_W, C_DIVIDER);
  tft.setFreeFont(&FreeSansBold9pt7b);
  tft.setTextSize(1);
  // Button 1 (left)
  if (btn1) {
    int bw1 = strlen(btn1) * 7 + 12; if (bw1 < 50) bw1 = 50;
    tft.fillRoundRect(6, SCR_H - FTR_H + 4, bw1, FTR_H - 8, 4, C_BTN_BG);
    tft.drawRoundRect(6, SCR_H - FTR_H + 4, bw1, FTR_H - 8, 4, C_DIVIDER);
    tft.setTextColor(C_WHITE, C_BTN_BG);
    tft.drawString(btn1, 6 + (bw1 - tft.textWidth(btn1)) / 2, SCR_H - FTR_H + 8);
  }
  // Button 2 (center)
  if (btn2) {
    int bw2 = strlen(btn2) * 7 + 12; if (bw2 < 60) bw2 = 60;
    int bx2 = (SCR_W - bw2) / 2;
    tft.fillRoundRect(bx2, SCR_H - FTR_H + 4, bw2, FTR_H - 8, 4, C_BTN_BG);
    tft.drawRoundRect(bx2, SCR_H - FTR_H + 4, bw2, FTR_H - 8, 4, C_DIVIDER);
    tft.setTextColor(C_WHITE, C_BTN_BG);
    tft.drawString(btn2, bx2 + (bw2 - tft.textWidth(btn2)) / 2, SCR_H - FTR_H + 8);
  }
  // Button 3 (right)
  if (btn3) {
    int bw3 = strlen(btn3) * 7 + 12; if (bw3 < 50) bw3 = 50;
    int bx3 = SCR_W - 6 - bw3;
    tft.fillRoundRect(bx3, SCR_H - FTR_H + 4, bw3, FTR_H - 8, 4, C_BTN_BG);
    tft.drawRoundRect(bx3, SCR_H - FTR_H + 4, bw3, FTR_H - 8, 4, C_DIVIDER);
    tft.setTextColor(C_WHITE, C_BTN_BG);
    tft.drawString(btn3, bx3 + (bw3 - tft.textWidth(btn3)) / 2, SCR_H - FTR_H + 8);
  }
}

// Check footer button hit (returns 1,2,3 or 0)
int footerHit(const char* btn1, const char* btn2, const char* btn3, int tx, int ty) {
  if (ty < SCR_H - FTR_H || ty > SCR_H) return 0;
  if (btn1) {
    int bw1 = strlen(btn1) * 7 + 12; if (bw1 < 50) bw1 = 50;
    if (tx >= 6 && tx <= 6 + bw1) return 1;
  }
  if (btn2) {
    int bw2 = strlen(btn2) * 7 + 12; if (bw2 < 60) bw2 = 60;
    int bx2 = (SCR_W - bw2) / 2;
    if (tx >= bx2 && tx <= bx2 + bw2) return 2;
  }
  if (btn3) {
    int bw3 = strlen(btn3) * 7 + 12; if (bw3 < 50) bw3 = 50;
    int bx3 = SCR_W - 6 - bw3;
    if (tx >= bx3 && tx <= bx3 + bw3) return 3;
  }
  return 0;
}

void drawHeader(const char* title, const char* rightText = NULL) {
  tft.fillRect(0, 0, SCR_W, HDR_H, C_HEADER);
  // CAN status dot on the left
  tft.fillCircle(8, 13, 4, canOnline() ? C_GREEN : C_RED);
  tft.setTextSize(1);
  tft.setFreeFont(&FreeSansBold9pt7b);
  tft.setTextColor(C_WHITE, C_HEADER);
  tft.drawString(title, 18, 6);
  if (rightText) {
    int rw = tft.textWidth(rightText);
    tft.drawString(rightText, SCR_W - rw - 8, 6);
  }
}

void drawDivider(int y) {
  tft.drawFastHLine(4, y, SCR_W - 8, C_DIVIDER);
}

// Generic menu button

void drawBtn(const char* label, int x, int y, int w, int h, uint16_t bg, uint16_t fg, int sz) {
  // Fill entire area
  tft.fillRoundRect(x, y, w, h, 4, bg);
  tft.drawRoundRect(x, y, w, h, 4, C_DIVIDER);
  // Center text
  tft.setTextSize(sz);
  tft.setTextColor(fg, bg);
  int lw = tft.textWidth(label);
  int lh = sz * 8;
  int tx = x + (w - lw) / 2;
  int ty = y + (h - lh) / 2;
  if (ty < y) ty = y + 2;
  tft.drawString(label, tx, ty);
}

bool btnHit(int x, int y, int w, int h, int tx, int ty) {
  return (tx >= x && tx <= x + w && ty >= y && ty <= y + h);
}

void sortByChanges() {
  sortedCount = scanCount;
  for (int i = 0; i < sortedCount; i++) sortedIdx[i] = i;
  for (int i = 0; i < sortedCount - 1; i++) {
    for (int j = 0; j < sortedCount - 1 - i; j++) {
      if (scanBuf[sortedIdx[j]].changes < scanBuf[sortedIdx[j + 1]].changes) {
        int t = sortedIdx[j]; sortedIdx[j] = sortedIdx[j + 1]; sortedIdx[j + 1] = t;
      }
    }
  }
}

int findId(unsigned long id) {
  for (int i = 0; i < scanCount; i++)
    if (scanBuf[i].id == id) return i;
  return -1;
}

// ═══════════════════════════════════════════════════════════════
//  CAN CORE (MCP2515 + FIFO)
// ═══════════════════════════════════════════════════════════════

bool initCAN() {
  pinMode(CAN_CS, OUTPUT);
  digitalWrite(CAN_CS, HIGH);
  delay(50);
  if (CAN0.begin(CAN_500KBPS, MCP_8MHz) != CAN_OK) return false;
  CAN0.init_Mask(0, 0, 0x00000000);
  CAN0.init_Mask(1, 0, 0x00000000);
  CAN0.setMode(MODE_LISTENONLY);
  return true;
}

bool canOnline() {
  return (CAN0.checkReceive() != CAN_NOMSG);
}

// ═══════════════════════════════════════════════════════════════
//  DECODER - Honda CAN table
// ═══════════════════════════════════════════════════════════════

void decodeMessage(unsigned long id, uint8_t* data, int len) {
  decodedCount = 0;
  switch (id) {
    case 0x17C:
      if (len >= 4) {
        uint16_t rpm = ((uint16_t)data[2] << 8) | data[3];
        decodedVals[decodedCount++] = {"RPM", (float)rpm, "rpm"};
        decodedVals[decodedCount++] = {"Gas", (float)data[0], "%"};
      }
      break;
    case 0x309:
      if (len >= 1) decodedVals[decodedCount++] = {"Speed", (float)data[0], "km/h"};
      break;
    case 0x324:
      if (len >= 2) {
        int coolant = data[0] - 40;
        decodedVals[decodedCount++] = {"Coolant", (float)coolant, "C"};
        decodedVals[decodedCount++] = {"Intake", (float)(data[1] - 40), "C"};
      }
      break;
    case 0x1A6:
      if (len >= 6) {
        int fuel = map(data[5], 0, 255, 0, 100);
        decodedVals[decodedCount++] = {"Fuel", (float)fuel, "%"};
        decodedVals[decodedCount++] = {"Cruise", data[0] & 0x80 ? 1.0f : 0, ""};
      }
      break;
    case 0x191:
      if (len >= 1) {
        const char* gears[] = {"P", "R", "N", "D", "2", "1", "L", "S"};
        int g = data[0] & 0x07;
        decodedVals[decodedCount++] = {"Gear", (float)g, (g < 8) ? gears[g] : "?"};
      }
      break;
    case 0x1A4:
      if (len >= 1) decodedVals[decodedCount++] = {"Brake", (float)map(data[0], 0, 255, 0, 100), "%"};
      break;
    case 0x158:
      if (len >= 6) {
        // BYTE[4:5]=SPEEDOMETER(km/h*0.01)
        uint16_t spd = ((uint16_t)data[4] << 8) | data[5];
        decodedVals[decodedCount++] = {"Speed", (float)spd * 0.01f, "km/h"};
        if (len >= 7) decodedVals[decodedCount++] = {"Trip", (float)(data[6] * 10), "km"};
      }
      break;
    case 0x13C:
      if (len >= 1) decodedVals[decodedCount++] = {"Throttle", (float)data[0] * 100.0f / 255.0f, "%"};
      break;
    case 0x18E:
      if (len >= 4) {
        float lat = (float)(int8_t)data[0] * 0.01f;
        float lon = (float)(int8_t)data[1] * 0.01f;
        decodedVals[decodedCount++] = {"Lat G", lat, "g"};
        decodedVals[decodedCount++] = {"Lon G", lon, "g"};
      }
      break;
    case 0x294:
      if (len >= 4) {
        if (data[0] & 0x01) decodedVals[decodedCount++] = {"Turn", 1, "L"};
        if (data[0] & 0x02) decodedVals[decodedCount++] = {"Turn", 1, "R"};
        // BYTE[1] = dash brightness: 0(dim)..15(bright) with headlights ON, 0x60=MAX overdrive, ~16 when headlights OFF
        decodedVals[decodedCount++] = {"Dimmer", (float)data[1], ""};
        uint32_t odo = ((uint32_t)data[1] << 16) | ((uint32_t)data[2] << 8) | data[3];
        decodedVals[decodedCount++] = {"Odometer", (float)odo, "km"};
      }
      break;
    case 0x255:
      if (len >= 8) {
        decodedVals[decodedCount++] = {"FL", (float)((data[0]<<8)|data[1]), "km/h"};
        decodedVals[decodedCount++] = {"FR", (float)((data[2]<<8)|data[3]), "km/h"};
        decodedVals[decodedCount++] = {"RL", (float)((data[4]<<8)|data[5]), "km/h"};
        decodedVals[decodedCount++] = {"RR", (float)((data[6]<<8)|data[7]), "km/h"};
      }
      break;
    default:
      for (int i = 0; i < len && i < 8; i++) {
        if (decodedCount < 4) {
          char lbl[4]; sprintf(lbl, "B%d", i);
          decodedVals[decodedCount++] = {lbl, (float)data[i], ""};
        }
      }
      break;
  }
}

// ═══════════════════════════════════════════════════════════════
//  MODE: MAIN MENU
// ═══════════════════════════════════════════════════════════════

void drawMenu() {
  tft.setFreeFont(&FreeSansBold9pt7b);
  tft.setTextSize(1);
  drawHeader("CAN-MULTITOOL", "v2.2");

  int itemsCount = MENU_ITEM_COUNT;

  // Layout: 2 columns x 3 rows (6 items per page)
  int colW = 152;
  int rowH = 48;
  int gapX = 6;
  int gapY = 3;
  int startX = 6;
  int startY = HDR_H + 3;
  int perPage = MENU_PER_PAGE; // 6 buttons per page
  int cols = 2;
  int pageStart = menuPage * perPage;

  for (int i = 0; i < perPage && (pageStart + i) < itemsCount; i++) {
    int idx = pageStart + i;
    int col = i % cols;
    int row = i / cols;
    int bx = startX + col * (colW + gapX);
    int by = startY + row * (rowH + gapY);

    uint16_t bg = (idx % 2 == 0) ? C_BTN_BG : 0x2108;

    tft.fillRoundRect(bx, by, colW, rowH, 4, bg);
    tft.drawRoundRect(bx, by, colW, rowH, 4, C_DIVIDER);

    tft.setTextSize(1);
    if (menuItems[idx].ready) {
      tft.setTextColor(C_GREEN, bg);
      tft.drawString("+", bx + 4, by + (rowH - 8) / 2);
    } else {
      tft.setTextColor(C_RED, bg);
      tft.drawString("-", bx + 4, by + (rowH - 8) / 2);
    }

    tft.setTextSize(1);
    tft.setTextColor(C_WHITE, bg);
    int lw = tft.textWidth(menuItems[idx].label);
    int lx = bx + (colW - lw) / 2;
    if (lx < bx + 16) lx = bx + 16;
    tft.drawString(menuItems[idx].label, lx, by + (rowH - 8) / 2);
  }

  int totalPages = (itemsCount + perPage - 1) / perPage;
  if (totalPages > 1) {
    drawFooter((menuPage > 0) ? "<" : NULL,
               "MENU",
               (menuPage < totalPages - 1) ? ">" : NULL);
  } else {
    drawFooter(NULL, "MENU", NULL);
  }
}

void handleMenuTouch(int tx, int ty) {
  int itemsCount = MENU_ITEM_COUNT;
  int perPage = MENU_PER_PAGE;
  int cols = 2;
  int colW = 152; int rowH = 48; int gapX = 6; int gapY = 3;
  int startX = 6; int startY = HDR_H + 3;
  int pageStart = menuPage * perPage;

  for (int i = 0; i < perPage && (pageStart + i) < itemsCount; i++) {
    int idx = pageStart + i;
    int col = i % cols;
    int row = i / cols;
    int bx = startX + col * (colW + gapX);
    int by = startY + row * (rowH + gapY);
    if (btnHit(bx, by, colW, rowH, tx, ty)) {
      if (menuItems[idx].ready) {
        mode = menuItems[idx].target;
        redrawNeeded = true;
        if (mode == MODE_SCANNING) {
          scanCount = 0; sortedCount = 0;
          scanStartMs = millis();
        }
      }
      return;
    }
  }

  int totalPages = (itemsCount + perPage - 1) / perPage;
  int f = footerHit((menuPage > 0) ? "<" : NULL, "MENU", (menuPage < totalPages - 1) ? ">" : NULL, tx, ty);
  if (f == 1 && menuPage > 0) { menuPage--; redrawNeeded = true; }
  if (f == 3 && menuPage < totalPages - 1) { menuPage++; redrawNeeded = true; }
}

// ═══════════════════════════════════════════════════════════════
//  MODE: CAN ID SCANNER
// ═══════════════════════════════════════════════════════════════

void drawScanner() {
  tft.setFreeFont(&FreeSansBold9pt7b);
  tft.setTextSize(1);
  char tmp[48];
  sprintf(tmp, "SCANNING: %lds", (scanDurationMs - (millis() - scanStartMs)) / 1000);
  drawHeader(tmp, NULL);

  // Pulse dot
  tft.fillCircle(18, 50, 4, pulseState ? C_GREEN : C_HEADER);
  tft.setTextColor(C_GREY, C_BG);
  tft.drawString("Listening for CAN IDs...", 30, 46);

  // Progress bar
  int pct = ((millis() - scanStartMs) * 300) / scanDurationMs;
  if (pct > 300) pct = 300;
  tft.fillRect(10, 80, 300, 20, C_LIST_BG);
  tft.drawRect(10, 80, 300, 20, C_WHITE);
  if (pct > 0) tft.fillRect(10, 80, pct, 20, C_ACCENT);

  // ID counter
  sprintf(tmp, "Found: %d IDs", scanCount);
  tft.setTextSize(1);
  tft.setTextColor(C_WHITE, C_BG);
  tft.drawString(tmp, 10, 110);

  tft.setTextSize(2);
  tft.setTextColor(C_GREY, C_BG);
  tft.drawString("Rotate dimmer wheel NOW", 10, 135);

  // Footer with STOP button
  drawFooter("STOP", NULL, NULL);
}

void handleScannerTouch(int tx, int ty) {
  int f = footerHit("STOP", NULL, NULL, tx, ty);
  if (f == 1) {
    if (scanCount > 0) {
      sortByChanges();
      listPage = 0;
      mode = MODE_LIST;
    } else {
      mode = MODE_MENU;
    }
    redrawNeeded = true;
  }
}

// ═══════════════════════════════════════════════════════════════
//  MODE: ID LIST
// ═══════════════════════════════════════════════════════════════

void drawList() {
  tft.setFreeFont(&FreeSansBold9pt7b);
  tft.setTextSize(1);
  int pages = sortedCount > 0 ? (sortedCount + PER_PAGE - 1) / PER_PAGE : 1;
  char pageStr[16];
  sprintf(pageStr, "%d/%d", listPage + 1, pages);
  drawHeader("FOUND IDs (by changes)", pageStr);

  int start = listPage * PER_PAGE;
  int itemsOnPage = sortedCount - start;
  if (itemsOnPage > PER_PAGE) itemsOnPage = PER_PAGE;

  if (sortedCount == 0) {
    centerText("No CAN messages received.", 90, C_GREY, C_BG, 1);
    centerText("Check connection and try again.", 110, C_GREY, C_BG, 1);
  } else {
    int ry = HDR_H + 6;
    int rowH = 32;
    int col1X = 14;   // ID
    int col2X = 120;  // count/changes

    for (int i = 0; i < itemsOnPage; i++) {
      int idx = start + i;
      int si = sortedIdx[idx];
      ScanEntry* e = &scanBuf[si];
      uint16_t rowBg = (i % 2 == 0) ? C_BG : C_LIST_BG;

      // Row background
      tft.fillRoundRect(6, ry, SCR_W - 12, rowH, 4, rowBg);
      tft.drawRoundRect(6, ry, SCR_W - 12, rowH, 4, C_DIVIDER);

      // ID (smaller to fit row)
      char tmp[12];
      sprintf(tmp, "0x%03lX", e->id);
      tft.setTextSize(1);
      tft.setTextColor(C_ACCENT, rowBg);
      tft.drawString(tmp, col1X, ry + 4);

      // Stats
      tft.setTextSize(1);
      sprintf(tmp, "cnt:%lu  chg:%lu", e->count, e->changes);
      tft.setTextColor(C_GREY, rowBg);
      tft.drawString(tmp, col2X, ry + 4);

      // Hex preview
      char hex[18]; hex[0] = 0;
      for (int b = 0; b < e->firstSeen && b < 4; b++) {
        sprintf(hex + strlen(hex), "%02X ", e->lastData[b]);
      }
      tft.setTextColor(C_GREY, rowBg);
      tft.drawString(hex, col2X, ry + 18);

      ry += rowH + 4;
    }
  }

  // Footer navigation
  if (pages > 1) {
    drawFooter(listPage > 0 ? "<" : NULL,
               "MENU",
               listPage + 1 < pages ? ">" : NULL);
  } else {
    drawFooter(NULL, "MENU", NULL);
  }
}

void handleListTouch(int tx, int ty) {
  int pages = sortedCount > 0 ? (sortedCount + PER_PAGE - 1) / PER_PAGE : 1;
  int start = listPage * PER_PAGE;
  int itemsOnPage = sortedCount - start;
  if (itemsOnPage > PER_PAGE) itemsOnPage = PER_PAGE;

  // Check row taps
  int ry = HDR_H + 6;
  int rowH = 32;
  for (int i = 0; i < itemsOnPage; i++) {
    if (btnHit(6, ry, SCR_W - 12, rowH, tx, ty)) {
      if (sortedCount > 0) {
        int si = sortedIdx[start + i];
        monitorId = scanBuf[si].id;
        monitorHasData = false;
        mode = MODE_MONITOR_RAW;
        redrawNeeded = true;
      }
      return;
    }
    ry += rowH + 4;
  }

  // Footer
  int f = footerHit(listPage > 0 ? "<" : NULL,
                    "MENU",
                    listPage + 1 < pages ? ">" : NULL, tx, ty);
  if (f == 1 && listPage > 0) { listPage--; redrawNeeded = true; }
  if (f == 2) { mode = MODE_MENU; redrawNeeded = true; }
  if (f == 3 && listPage + 1 < pages) { listPage++; redrawNeeded = true; }
}

// ═══════════════════════════════════════════════════════════════
//  MODE: MONITOR RAW (bar 0-255)
// ═══════════════════════════════════════════════════════════════

#define MON_HEX_X    10
#define MON_HEX_Y    50

void drawMonitorRaw() {
  char tmp[48];
  sprintf(tmp, "MONITOR: 0x%03lX", monitorId);
  drawHeader(tmp, NULL);

  // Hex bytes
  tft.setFreeFont(&FreeSansBold9pt7b);
  tft.setTextSize(1);
  tft.setTextColor(C_GREY, C_BG);
  if (monitorHasData) {
    char hex[40]; hex[0] = 0;
    for (int i = 0; i < monitorLen; i++) {
      sprintf(hex + strlen(hex), "%02X ", monitorBytes[i]);
    }
    sprintf(tmp, "HEX: %s", hex);
    tft.drawString(tmp, 10, 50);
  }

  // Byte index labels
  tft.setFreeFont(&FreeSansBold9pt7b);
  tft.setTextSize(1);
  tft.setTextColor(C_DIVIDER, C_BG);
  if (monitorHasData) {
    char idx[30]; idx[0] = 0;
    for (int i = 0; i < monitorLen; i++) {
      sprintf(idx + strlen(idx), "%d  ", i);
    }
    sprintf(tmp, "IDX: %s", idx);
    tft.drawString(tmp, 10, 50 + 28);
  }

  drawFooter("BACK", NULL, NULL);
}

void updateMonitorRawValue() {
  if (!monitorHasData) return;
  char tmp[48];
  char hex[40]; hex[0] = 0;
  for (int i = 0; i < monitorLen; i++) {
    sprintf(hex + strlen(hex), "%02X ", monitorBytes[i]);
  }
  tft.fillRect(10, 50, 300, 22, C_BG);
  tft.setFreeFont(&FreeSansBold9pt7b);
  tft.setTextSize(1);
  tft.setTextColor(C_GREY, C_BG);
  sprintf(tmp, "HEX: %s", hex);
  tft.drawString(tmp, 10, 50);
}

void handleMonitorRawTouch(int tx, int ty) {
  int f = footerHit("BACK", NULL, NULL, tx, ty);
  if (f == 1) { mode = MODE_LIST; redrawNeeded = true; }
}

// ═══════════════════════════════════════════════════════════════
// ═══════════════════════════════════════════════════════════════
//  MODE: DIMMER (0x294 BYTE[1] - подсветка приборки)
// ═══════════════════════════════════════════════════════════════

void drawDimmer() {
  drawHeader("DASH DIMMER", NULL);

  // Big number (Font7 like speedo)
  tft.setTextFont(7);
  tft.setTextSize(2);
  char tmp[10];
  sprintf(tmp, "0x%02X", dimmerValue);
  int tw = tft.textWidth(tmp);
  tft.setTextColor(C_ACCENT, C_BG);
  tft.drawString(tmp, (SCR_W - tw) / 2, 60);

  // Bar behind the number
  int barW = 300;
  int barH = 30;
  int barX = 10;
  int barY = 120;

  tft.fillRect(barX, barY, barW, barH, C_LIST_BG);
  tft.drawRect(barX, barY, barW, barH, C_DIVIDER);

  // Fill portion
  int fillW = map(dimmerValue, 0, 150, 0, barW);
  if (fillW > barW) fillW = barW;
  if (fillW < 0) fillW = 0;
  uint16_t barColor = (dimmerValue > 0x60) ? C_RED : C_ACCENT;
  if (fillW > 0) tft.fillRect(barX + 1, barY + 1, fillW - 2, barH - 2, barColor);

  // Label
  tft.setFreeFont(&FreeSansBold9pt7b);
  tft.setTextSize(1);
  tft.setTextColor(C_GREY, C_BG);
  sprintf(tmp, "Bright: %d  (0=%s  15=%s  0x60=MAX)", dimmerValue, "OFF", "MAX");
  tft.drawString(tmp, 10, 160);

  drawFooter(NULL, "MENU", NULL);
}

void updateDimmerValue() {
  if (dimmerValue == prevBrightness) return;
  prevBrightness = dimmerValue;

  // Update big number
  tft.fillRect(10, 50, 300, 60, C_BG);
  tft.setTextFont(7);
  tft.setTextSize(2);
  char tmp[10];
  sprintf(tmp, "0x%02X", dimmerValue);
  int tw = tft.textWidth(tmp);
  tft.setTextColor(C_ACCENT, C_BG);
  tft.drawString(tmp, (SCR_W - tw) / 2, 60);

  // Update bar
  int barW = 300;
  int barH = 30;
  int barX = 10;
  int barY = 120;
  tft.fillRect(barX, barY, barW, barH, C_LIST_BG);
  tft.drawRect(barX, barY, barW, barH, C_DIVIDER);
  int fillW = map(dimmerValue, 0, 150, 0, barW);
  if (fillW > barW) fillW = barW;
  if (fillW < 0) fillW = 0;
  uint16_t barColor = (dimmerValue > 0x60) ? C_RED : C_ACCENT;
  if (fillW > 0) tft.fillRect(barX + 1, barY + 1, fillW - 2, barH - 2, barColor);

  // Update label
  tft.fillRect(10, 155, 300, 20, C_BG);
  tft.setFreeFont(&FreeSansBold9pt7b);
  tft.setTextSize(1);
  tft.setTextColor(C_GREY, C_BG);
  sprintf(tmp, "Bright: %d  (0=%s  15=%s  0x60=MAX)", dimmerValue, "OFF", "MAX");
  tft.drawString(tmp, 10, 160);
}

void handleDimmerTouch(int tx, int ty) {
  int f = footerHit(NULL, "MENU", NULL, tx, ty);
  if (f == 2) { mode = MODE_MENU; redrawNeeded = true; }
}

// ═══════════════════════════════════════════════════════════════
//  MODE: SPEEDOMETER
// ═══════════════════════════════════════════════════════════════

// speedo values reset on mode enter
#define SPEEDO_MODE_SPEED 0
#define SPEEDO_MODE_RPM   1
#define SPEEDO_MODE_BOTH  2
uint8_t speedoMode = SPEEDO_MODE_SPEED;

void drawSpeedo() {
  drawHeader("SPEEDOMETER", NULL);

  char tmp[10];
  int mainY;

  if (speedoMode == SPEEDO_MODE_SPEED) {
    mainY = 80;
    tft.setTextFont(7);
    tft.setTextSize(2);
    sprintf(tmp, "%4d", speedoValue);
    int tw = tft.textWidth(tmp);
    tft.setTextColor(C_ACCENT, C_BG);
    tft.drawString(tmp, (SCR_W - tw) / 2, mainY);

  } else if (speedoMode == SPEEDO_MODE_RPM) {
    mainY = 80;
    tft.setTextFont(7);
    tft.setTextSize(2);
    sprintf(tmp, "%4d", rpmValue);
    int tw = tft.textWidth(tmp);
    tft.setTextColor(C_GREEN, C_BG);
    tft.drawString(tmp, (SCR_W - tw) / 2, mainY);

  } else if (speedoMode == SPEEDO_MODE_BOTH) {
    mainY = 50;
    tft.setTextFont(7);
    tft.setTextSize(1);
    sprintf(tmp, "%4d", speedoValue);
    int tw = tft.textWidth(tmp);
    tft.setTextColor(C_ACCENT, C_BG);
    tft.drawString(tmp, (SCR_W - tw) / 2, mainY);

    mainY = 140;
    tft.setTextSize(1);
    sprintf(tmp, "%4d", rpmValue);
    tw = tft.textWidth(tmp);
    tft.setTextColor(C_GREEN, C_BG);
    tft.drawString(tmp, (SCR_W - tw) / 2, mainY);
  }

  drawSpeedoFooter();
}

void updateSpeedoValue() {
  // Redraw only the number, not header/footer
  char tmp[10];
  int mainY, textSz;

  if (speedoMode == SPEEDO_MODE_SPEED) {
    mainY = 80; textSz = 2;
    tft.fillRect(10, 70, 300, 60, C_BG);
    tft.setTextFont(7);
    tft.setTextSize(textSz);
    sprintf(tmp, "%4d", speedoValue);
    int tw = tft.textWidth(tmp);
    tft.setTextColor(C_ACCENT, C_BG);
    tft.drawString(tmp, (SCR_W - tw) / 2, mainY);

  } else if (speedoMode == SPEEDO_MODE_RPM) {
    mainY = 80; textSz = 2;
    tft.fillRect(10, 70, 300, 60, C_BG);
    tft.setTextFont(7);
    tft.setTextSize(textSz);
    sprintf(tmp, "%4d", rpmValue);
    int tw = tft.textWidth(tmp);
    tft.setTextColor(C_GREEN, C_BG);
    tft.drawString(tmp, (SCR_W - tw) / 2, mainY);

  } else if (speedoMode == SPEEDO_MODE_BOTH) {
    tft.fillRect(10, 40, 300, 170, C_BG);

    mainY = 50; textSz = 1;
    tft.setTextFont(7);
    tft.setTextSize(textSz);
    sprintf(tmp, "%4d", speedoValue);
    int tw = tft.textWidth(tmp);
    tft.setTextColor(C_ACCENT, C_BG);
    tft.drawString(tmp, (SCR_W - tw) / 2, mainY);

    mainY = 140; textSz = 1;
    tft.setTextFont(7);
    tft.setTextSize(textSz);
    sprintf(tmp, "%4d", rpmValue);
    tw = tft.textWidth(tmp);
    tft.setTextColor(C_GREEN, C_BG);
    tft.drawString(tmp, (SCR_W - tw) / 2, mainY);
  }
}

void drawSpeedoFooter() {
  tft.fillRect(0, SCR_H - FTR_H, SCR_W, FTR_H, C_FOOTER);
  tft.drawFastHLine(0, SCR_H - FTR_H, SCR_W, C_DIVIDER);
  tft.setFreeFont(&FreeSansBold9pt7b);
  tft.setTextSize(1);
  int bw = 44; int gap = 4; int sx = 6;
  // SPD
  uint16_t c1 = (speedoMode == SPEEDO_MODE_SPEED) ? 0x4208 : C_BTN_BG;
  tft.fillRoundRect(sx, SCR_H - FTR_H + 4, bw, FTR_H - 8, 4, c1);
  tft.drawRoundRect(sx, SCR_H - FTR_H + 4, bw, FTR_H - 8, 4, C_DIVIDER);
  tft.setTextColor(C_WHITE, c1);
  tft.drawString("SPD", sx + (bw - tft.textWidth("SPD")) / 2, SCR_H - FTR_H + 8);
  // BOTH
  sx += bw + gap;
  uint16_t c2 = (speedoMode == SPEEDO_MODE_BOTH) ? 0x4208 : C_BTN_BG;
  tft.fillRoundRect(sx, SCR_H - FTR_H + 4, bw, FTR_H - 8, 4, c2);
  tft.drawRoundRect(sx, SCR_H - FTR_H + 4, bw, FTR_H - 8, 4, C_DIVIDER);
  tft.setTextColor(C_WHITE, c2);
  tft.drawString("BTH", sx + (bw - tft.textWidth("BTH")) / 2, SCR_H - FTR_H + 8);
  // RPM
  sx += bw + gap;
  uint16_t c3 = (speedoMode == SPEEDO_MODE_RPM) ? 0x4208 : C_BTN_BG;
  tft.fillRoundRect(sx, SCR_H - FTR_H + 4, bw, FTR_H - 8, 4, c3);
  tft.drawRoundRect(sx, SCR_H - FTR_H + 4, bw, FTR_H - 8, 4, C_DIVIDER);
  tft.setTextColor(C_WHITE, c3);
  tft.drawString("RPM", sx + (bw - tft.textWidth("RPM")) / 2, SCR_H - FTR_H + 8);
  // MENU (red, right-aligned)
  int mx = SCR_W - 6 - 50;
  tft.fillRoundRect(mx, SCR_H - FTR_H + 4, 50, FTR_H - 8, 4, C_RED);
  tft.drawRoundRect(mx, SCR_H - FTR_H + 4, 50, FTR_H - 8, 4, C_DIVIDER);
  tft.setFreeFont(&FreeSansBold9pt7b);
  tft.setTextSize(1);
  tft.setTextColor(C_WHITE, C_RED);
  tft.drawString("MENU", mx + (50 - tft.textWidth("MENU")) / 2, SCR_H - FTR_H + 8);
}



// ═══════════════════════════════════════════════════════════════
//  MODE: SENSORS
// ═══════════════════════════════════════════════════════════════

void drawSensors() {
  tft.setFreeFont(&FreeSansBold9pt7b);
  tft.setTextSize(1);
  drawHeader("ENGINE SENSORS", NULL);

  int cols = 2;
  int cellW = 148;
  int cellH = 50;
  int gapX = 12;
  int gapY = 3;
  int startX = (SCR_W - (cols * cellW + (cols - 1) * gapX)) / 2;
  int startY = 28;

  for (int i = 0; i < SENSOR_SLOTS; i++) {
    int col = i % cols;
    int row = i / cols;
    int cx = startX + col * (cellW + gapX);
    int cy = startY + row * (cellH + gapY);

    uint16_t cellBg = C_LIST_BG;
    if (sensorCanId[i] == 0) cellBg = 0x1864;
    tft.fillRoundRect(cx, cy, cellW, cellH, 4, cellBg);
    tft.drawRoundRect(cx, cy, cellW, cellH, 4, C_DIVIDER);

    // Label (smooth)
    tft.setFreeFont(&FreeSansBold9pt7b);
    tft.setTextSize(1);
    tft.setTextColor(C_GREY, cellBg);
    tft.drawString(sensorLabels[i], cx + 8, cy + 3);

    if (sensorCanId[i] == 0) {
      // No ID assigned — prompt
      tft.setFreeFont(&FreeSansBold9pt7b);
      tft.setTextSize(1);
      tft.setTextColor(C_ACCENT, cellBg);
      tft.drawString("--- tap --->", cx + 8, cy + 30);
      continue;
    }

    // Value + unit
    char val[16];
    sprintf(val, "%d%s", sensorValue[i], sensorUnits[i]);
    tft.setFreeFont(&FreeSansBold12pt7b);
    tft.setTextSize(1);
    tft.setTextColor(C_WHITE, cellBg);
    tft.drawString(val, cx + 8, cy + 30);
  }

  drawFooter(NULL, "MENU", NULL);
}

// ═══════════════════════════════════════════════════════════════
//  MODE: SENSOR PICKER (select CAN ID for a sensor slot)
// ═══════════════════════════════════════════════════════════════

void drawSensorPicker() {
  tft.setFreeFont(&FreeSansBold9pt7b);
  tft.setTextSize(1);
  drawHeader("SELECT CAN ID", NULL);

  if (scanCount == 0) {
    centerText("No scanned IDs!", 80, C_RED, C_BG, 1);
    centerText("Run SCAN ID mode first", 100, C_GREY, C_BG, 1);
    drawFooter(NULL, "MENU", NULL);
    return;
  }

  int itemsPerPage = 5;
  int totalPages = (scanCount + itemsPerPage - 1) / itemsPerPage;
  if (menuPage >= totalPages) menuPage = totalPages - 1;
  if (menuPage < 0) menuPage = 0;

  int start = menuPage * itemsPerPage;
  int end = start + itemsPerPage;
  if (end > scanCount) end = scanCount;

  int ry = HDR_H + 8;
  int rowH = 30;
  char tmp[24];
  tft.setFreeFont(&FreeSansBold9pt7b);
  tft.setTextSize(1);

  for (int i = start; i < end; i++) {
    uint16_t bg = (i % 2 == 0) ? C_BG : C_LIST_BG;
    tft.fillRect(10, ry, 300, rowH, bg);

    sprintf(tmp, "0x%03lX", scanBuf[i].id);
    tft.setTextColor(C_ACCENT, bg);
    tft.drawString(tmp, 14, ry + 6);

    sprintf(tmp, "cnt:%lu", scanBuf[i].count);
    tft.setTextColor(C_GREY, bg);
    tft.drawString(tmp, 110, ry + 6);

    ry += rowH;
  }

  // Navigation footer
  if (totalPages > 1) {
    drawFooter(menuPage > 0 ? "<--" : NULL,
               "MENU",
               menuPage + 1 < totalPages ? "-->" : NULL);
  } else {
    drawFooter(NULL, "MENU", NULL);
  }
}

void handleSensorPickerTouch(int tx, int ty) {
  // Footer navigation
  int itemsPerPage = 5;
  int totalPages = max(1, (scanCount + itemsPerPage - 1) / itemsPerPage);

  int f = footerHit(menuPage > 0 ? "<--" : NULL,
                    "MENU",
                    menuPage + 1 < totalPages ? "-->" : NULL, tx, ty);
  if (f == 1 && menuPage > 0) { menuPage--; redrawNeeded = true; return; }
  if (f == 2) { menuPage = 0; mode = MODE_SENSORS; redrawNeeded = true; return; }
  if (f == 3 && menuPage + 1 < totalPages) { menuPage++; redrawNeeded = true; return; }

  // Check row taps to select a CAN ID
  if (ty < HDR_H || ty > SCR_H - FTR_H) return;
  int start = menuPage * itemsPerPage;
  int end = start + itemsPerPage;
  if (end > scanCount) end = scanCount;

  int ry = HDR_H + 8;
  int rowH = 30;
  for (int i = start; i < end; i++) {
    if (tx >= 10 && tx <= 310 && ty >= ry && ty <= ry + rowH) {
      // Assign this CAN ID to the sensor slot
      if (sensorPickSlot >= 0 && sensorPickSlot < SENSOR_SLOTS) {
        sensorCanId[sensorPickSlot] = scanBuf[i].id;
        // Reset prev so first update shows instantly
        prevSensorValue[sensorPickSlot] = -1;
        sensorValue[sensorPickSlot] = 0;
        sensorPickSlot = -1;
        menuPage = 0;
        mode = MODE_SENSORS;
        redrawNeeded = true;
      }
      return;
    }
    ry += rowH;
  }
}

void updateSensorsValue() {
  // Redraw only the value areas (not labels/borders/header/footer)
  int cols = 2;
  int cellW = 148;
  int cellH = 50;
  int gapX = 12;
  int gapY = 3;
  int startX = (SCR_W - (cols * cellW + (cols - 1) * gapX)) / 2;
  int startY = 28;

  for (int i = 0; i < SENSOR_SLOTS; i++) {
    if (sensorCanId[i] == 0) continue;
    int col = i % cols;
    int row = i / cols;
    int cx = startX + col * (cellW + gapX);
    int cy = startY + row * (cellH + gapY);

    // Clear the value area (label + value)
    tft.fillRect(cx + 6, cy + 25, cellW - 12, 28, C_LIST_BG);

    // Value + unit
    char val[16];
    sprintf(val, "%d%s", sensorValue[i], sensorUnits[i]);
    tft.setFreeFont(&FreeSansBold12pt7b);
    tft.setTextSize(1);
    tft.setTextColor(C_WHITE, C_LIST_BG);
    tft.drawString(val, cx + 8, cy + 30);
  }
}

void handleSensorsTouch(int tx, int ty) {
  // Check footer (MENU) first
  int f = footerHit(NULL, "MENU", NULL, tx, ty);
  if (f == 2) { mode = MODE_MENU; redrawNeeded = true; return; }

  // Check cell taps
  int cols = 2;
  int cellW = 148;
  int cellH = 48;
  int gapX = 12;
  int gapY = 4;
  int startX = (SCR_W - (cols * cellW + (cols - 1) * gapX)) / 2;
  int startY = 30;

  for (int i = 0; i < SENSOR_SLOTS; i++) {
    int col = i % cols;
    int row = i / cols;
    int cx = startX + col * (cellW + gapX);
    int cy = startY + row * (cellH + gapY);
    if (tx >= cx && tx <= cx + cellW && ty >= cy && ty <= cy + cellH) {
      if (scanCount > 0) {
        sensorPickSlot = i;
        mode = MODE_SENSOR_PICK;
        redrawNeeded = true;
      }
      return;
    }
  }
}

// ═══════════════════════════════════════════════════════════════
//  CAN PROCESSING
// ═══════════════════════════════════════════════════════════════

void processCanData() {
  if (fifoHead == fifoTail) return;
  while (fifoHead != fifoTail) {
    CanMsg msg = fifo[fifoTail];
    fifoTail = (fifoTail + 1) % FIFO_SIZE;

    if (mode == MODE_SCANNING) {
      int idx = findId(msg.id);
      if (idx < 0) {
        if (scanCount < MAX_IDS) {
          idx = scanCount;
          scanBuf[idx].id = msg.id;
          scanBuf[idx].count = 0;
          scanBuf[idx].changes = 0;
          scanBuf[idx].firstSeen = false;
          scanCount++;
        } else continue;
      }
      scanBuf[idx].count++;
      if (!scanBuf[idx].firstSeen) {
        scanBuf[idx].firstSeen = true;
        memcpy(scanBuf[idx].lastData, msg.data, 8);
      } else {
        bool changed = false;
        for (int b = 0; b < msg.len && b < 8; b++) {
          if (msg.data[b] != scanBuf[idx].lastData[b]) { changed = true; break; }
        }
        if (changed) {
          scanBuf[idx].changes++;
          memcpy(scanBuf[idx].lastData, msg.data, 8);
        }
      }
    }

    // ALWAYS capture sensor/speedo values (independent of mode)
    // 0x158 = ENGINE_DATA: BYTE[4:5]=SPEEDOMETER(km/h*0.01) - PRIMARY speed on Accord 8
    if (msg.id == 0x158 && msg.len >= 6) {
      uint16_t spd = ((uint16_t)msg.data[4] << 8) | msg.data[5];
      int kph = (int)((float)spd * 0.01f);
      if (kph != prevSpeedoVal) { prevSpeedoVal = kph; speedoValue = kph; valueUpdateNeeded = true; }
    }
    if (msg.id == 0x17C && msg.len >= 4) { rpmValue = ((uint16_t)msg.data[2] << 8) | msg.data[3]; if (rpmValue != prevRpmVal) { prevRpmVal = rpmValue; valueUpdateNeeded = true; } }
    if (msg.id == 0x324 && msg.len >= 1) { coolantTemp = msg.data[0] - 40; if (coolantTemp != prevCoolantTemp) { prevCoolantTemp = coolantTemp; valueUpdateNeeded = true; } }
    if (msg.id == 0x294 && msg.len >= 2) { dimmerValue = msg.data[1]; if (dimmerValue != prevBrightness) { prevBrightness = dimmerValue; valueUpdateNeeded = true; } }
    // Configurable sensor slots: check each against incoming messages
    for (int si = 0; si < SENSOR_SLOTS; si++) {
      if (sensorCanId[si] != 0 && msg.id == sensorCanId[si]) {
        int b = sensorDataByte[si];
        if (b == -1) {
          // Dedicated decoder: copy from global sensor variables
          if (si == 0) { sensorValue[0] = speedoValue; }
          else if (si == 1) { sensorValue[1] = rpmValue; }
          else if (si == 2) { sensorValue[2] = coolantTemp; }
        } else {
          if (b >= msg.len) b = 0;
          sensorValue[si] = msg.data[b];
        }
        if (sensorValue[si] != prevSensorValue[si]) {
          prevSensorValue[si] = sensorValue[si];
          if (mode == MODE_SENSORS) valueUpdateNeeded = true;
        }
      }
    }

    // Capture for Monitor (mode-gated, uses monitorId)
    bool capture =
      (mode == MODE_MONITOR_RAW) && msg.id == monitorId;

    if (capture) {
      monitorHasData = true;
      monitorLen = msg.len;
      // Only trigger update if data differs
      bool monChanged = (displayRawVal != (int)msg.data[0]);
      if (!monChanged && !prevMonitorHasData) monChanged = true;
      displayRawVal = msg.data[0];
      brightnessVal = (msg.data[0] > 100) ? map(msg.data[0], 0, 255, 0, 100) : msg.data[0];
      memcpy(monitorBytes, msg.data, 8);
      prevMonitorHasData = true;
      if (monChanged && millis() - lastMonitorUpdate >= MONITOR_UPDATE_MS) {
        lastMonitorUpdate = millis();
        valueUpdateNeeded = true;
      }
    }
  }
}

// ═══════════════════════════════════════════════════════════════
//  SCREEN DISPATCHER
// ═══════════════════════════════════════════════════════════════

void drawScreen() {
  switch (mode) {
    case MODE_MENU:          drawMenu(); break;
    case MODE_SCANNING:      drawScanner(); break;
    case MODE_LIST:          drawList(); break;
    case MODE_MONITOR_RAW:   drawMonitorRaw(); break;
    case MODE_SPEEDO:        drawSpeedo(); break;
    case MODE_DIMMER:        drawDimmer(); break;
    case MODE_SENSORS:       drawSensors(); break;
    case MODE_SENSOR_PICK:   drawSensorPicker(); break;
    default:                 drawMenu(); break;
  }
}

// ═══════════════════════════════════════════════════════════════
//  TOUCH HANDLER
// ═══════════════════════════════════════════════════════════════

void handleTouch() {
  uint16_t tx, ty;
  if (!tft.getTouch(&tx, &ty, 4)) return;
  delay(60);
  while (tft.getTouch(&tx, &ty, 4)) delay(10);
  switch (mode) {
    case MODE_MENU:          handleMenuTouch(tx, ty); break;
    case MODE_SCANNING:      handleScannerTouch(tx, ty); break;
    case MODE_LIST:          handleListTouch(tx, ty); break;
    case MODE_MONITOR_RAW:   handleMonitorRawTouch(tx, ty); break;
    case MODE_SENSORS:      handleSensorsTouch(tx, ty); break;
    case MODE_SENSOR_PICK:  handleSensorPickerTouch(tx, ty); break;
    case MODE_DIMMER:       handleDimmerTouch(tx, ty); break;
    case MODE_SPEEDO: {
      // Custom footer buttons: SPD(6), BTH(52), RPM(98), MENU(SCR_W-56)
      int bw = 44; int gap = 4;
      if (ty >= SCR_H - FTR_H) {
        int sx = 6;
        if (tx >= sx && tx <= sx + bw) { speedoMode = SPEEDO_MODE_SPEED; redrawNeeded = true; break; }
        sx += bw + gap;
        if (tx >= sx && tx <= sx + bw) { speedoMode = SPEEDO_MODE_BOTH; redrawNeeded = true; break; }
        sx += bw + gap;
        if (tx >= sx && tx <= sx + bw) { speedoMode = SPEEDO_MODE_RPM; redrawNeeded = true; break; }
        int mx = SCR_W - 6 - 50;
        if (tx >= mx && tx <= mx + 50) { mode = MODE_MENU; redrawNeeded = true; break; }
      }
      break;
    }
    default:                 handleMenuTouch(tx, ty); break;
  }
}

// ═══════════════════════════════════════════════════════════════
//  MCP2515 DRAIN (FIFO)
// ═══════════════════════════════════════════════════════════════

void drainMCP() {
  if (spiBusy) return;
  spiBusy = true;
  byte len;
  byte buf[8];
  while (CAN0.checkReceive() == CAN_MSGAVAIL &&
         ((fifoHead + 1) % FIFO_SIZE) != fifoTail) {
    if (CAN0.readMsgBuf(&len, buf) == CAN_OK) {
      int nh = (fifoHead + 1) % FIFO_SIZE;
      if (nh != fifoTail) {
        fifo[fifoHead].id = CAN0.getCanId();
        fifo[fifoHead].len = len;
        if (len > 8) len = 8;
        memcpy(fifo[fifoHead].data, buf, len);
        fifoHead = nh;
      }
    } else break;
  }
  spiBusy = false;
}

// ═══════════════════════════════════════════════════════════════
//  SETUP
// ═══════════════════════════════════════════════════════════════

void setup() {
  Serial.begin(115200);
  delay(100);
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(C_BG);
  tft.setFreeFont(&FreeSansBold9pt7b);
  uint16_t calData[5] = { 275, 3526, 268, 3447, 1 };
  tft.setTouch(calData);
  if (!initCAN()) {
    tft.setFreeFont(&FreeSansBold12pt7b);
    tft.setTextSize(2);
    tft.setTextColor(C_RED, C_BG);
    centerText("CAN ERROR!", 80, C_RED, C_BG, 2);
    tft.setFreeFont(&FreeSansBold9pt7b);
    tft.setTextSize(1);
    centerText("Check wiring", 110, C_GREY, C_BG, 1);
  }
  redrawNeeded = true;
}

// ═══════════════════════════════════════════════════════════════
//  LOOP
// ═══════════════════════════════════════════════════════════════

void loop() {
  unsigned long now = millis();
  drainMCP();
  processCanData();

  // Scan timer
  if (mode == MODE_SCANNING && (now - scanStartMs >= scanDurationMs)) {
    sortByChanges();
    listPage = 0;
    mode = MODE_LIST;
    redrawNeeded = true;
    delay(20);
    return;
  }

  // Scanner live updates
  if (mode == MODE_SCANNING) {
    if (now - lastPulseMs >= 500) {
      lastPulseMs = now;
      pulseState = !pulseState;
      tft.fillCircle(18, 50, 4, pulseState ? C_GREEN : C_HEADER);
    }
    unsigned long remain = (scanStartMs + scanDurationMs > now)
      ? (scanStartMs + scanDurationMs - now) / 1000 : 0;
    int pct = ((now - scanStartMs) * 300) / scanDurationMs;
    if (pct > 300) pct = 300;
    tft.fillRect(10, 80, 300, 20, C_LIST_BG);
    tft.drawRect(10, 80, 300, 20, C_WHITE);
    if (pct > 0) tft.fillRect(10, 80, pct, 20, C_ACCENT);
    char tmp[24];
    sprintf(tmp, "Listening...  %2lus", (unsigned long)remain);
    tft.setFreeFont(&FreeSansBold9pt7b);
    tft.setTextSize(1);
    tft.setTextColor(C_WHITE, C_BG);
    tft.fillRect(10, 100, 300, 14, C_BG);
    tft.drawString(tmp, 10, 100);
    sprintf(tmp, "Found: %d IDs", scanCount);
    tft.drawString(tmp, 10, 115);
  }

  // Partial updates for monitor/speedo
  if (valueUpdateNeeded) {
    valueUpdateNeeded = false;
    switch (mode) {
      case MODE_MONITOR_RAW:   updateMonitorRawValue(); break;
      case MODE_SPEEDO:        updateSpeedoValue(); break;
      case MODE_DIMMER:        updateDimmerValue(); break;
      case MODE_SENSORS:       updateSensorsValue(); break;
      default: break;
    }
  }

  // Full redraw
  if (redrawNeeded) {
    redrawNeeded = false;
    tft.fillScreen(C_BG);
    drawScreen();
  }

  // Touch
  handleTouch();

  // Yield
  if (now - lastYieldMs >= 5) {
    lastYieldMs = now;
    yield();
  }
}

// ═══════════════════════════════════════════════════════════════
//  END OF CAN-MULTITOOL.INO
// ═══════════════════════════════════════════════════════════════

