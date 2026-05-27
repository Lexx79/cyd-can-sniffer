// ═══════════════════════════════════════════════════════════════
//  CAN-Multitool — бортовой диагностический центр на CYD
//  Версия 2.0-dev
//  Авторы: Kiro (⚡) + Sergey (@Lexxabk)
//  Автомобиль: Honda Accord 8 (2008-2012)
//  Аппаратно: ESP32-2432S028 (CYD) + MCP2515+TJA1050
//
//  Режимы:
//    1. Сканер CAN ID    4. Датчики         7. OBD2
//    2. Монитор значений  5. Поиск блоков    8. Тест ID
//    3. Спидометр         6. CAN-логгер      9. SLCAN
// ═══════════════════════════════════════════════════════════════

#include <SPI.h>
#include <TFT_eSPI.h>
#include <mcp_can.h>

// ── TFT ──
TFT_eSPI tft;
TFT_eSprite spr = TFT_eSprite(&tft);

// ── CAN SPI ──
#define CAN_CS        22
MCP_CAN CAN0(CAN_CS);

// ── Размеры экрана ──
#define SCR_W         320
#define SCR_H         240
#define HDR_H         26

// ── Цвета (темная тема) ──
#define C_BG          TFT_BLACK
#define C_HEADER      0x4A0A    // тёмно-бордовый
#define C_ACCENT      0xFB64    // оранжевый
#define C_DIVIDER     0x4208    // тёмно-серый
#define C_LIST_BG     0x1082    // чуть светлее чёрного
#define C_WHITE       TFT_WHITE
#define C_GREY        0xAD55    // серый
#define C_GREEN       TFT_GREEN
#define C_RED         TFT_RED
#define C_BLUE        TFT_BLUE
#define C_YELLOW      0xFFE0
#define C_BTN_BG      0x1CE7    // сине-серый для кнопок
#define C_BTN_HL      0x2529    // подсветка кнопки

// ── CAN FIFO (как в v1.2) ──
#define FIFO_SIZE     512
struct CanMsg {
  unsigned long id;
  uint8_t len;
  uint8_t data[8];
};
CanMsg fifo[FIFO_SIZE];
volatile int fifoHead = 0;
volatile int fifoTail = 0;

// ── Параметры сканирования ──
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

// ── Режимы ──
enum Mode : uint8_t {
  MODE_MENU = 0,
  MODE_SCANNING,
  MODE_LIST,
  MODE_MONITOR_RAW,    // бар 0-255 (как v1.2)
  MODE_MONITOR_DECODE, // текстовая расшифровка байт
  MODE_SPEEDO,
  MODE_SENSORS,
  MODE_BLOCKFIND,
  MODE_LOGGER,
  MODE_OBD2,
  MODE_PROBE,
  MODE_SLCAN,
  MODE_EMULATOR
};
Mode mode = MODE_MENU;

// ── Состояние экрана ──
bool redrawNeeded = true;
bool valueUpdateNeeded = false;
bool pulseState = false;
unsigned long lastPulseMs = 0;
unsigned long lastYieldMs = 0;
unsigned long lastMonitorUpdate = 0;
#define MONITOR_UPDATE_MS 50

// ── Данные монитора ──
unsigned long monitorId = 0;
int displayRawVal = 0;
int brightnessVal = 0;
uint8_t monitorBytes[8];
int monitorLen = 0;
bool monitorHasData = false;

// ── Данные декодера ──
struct DecodedValue {
  const char* label;
  float value;
  const char* unit;
};
DecodedValue decodedVals[4];
int decodedCount = 0;

// ── Навигация ──
int listPage = 0;
#define PER_PAGE 5
int menuPage = 0;
#define MENU_PER_PAGE 4

// ── SPI MUTEX ──
volatile bool spiBusy = false;

// ═══════════════════════════════════════════════════════════════
//  ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ
// ═══════════════════════════════════════════════════════════════

void centerText(const char* s, int y, uint16_t color, uint16_t bg, int sz) {
  tft.setTextSize(sz);
  tft.setTextColor(color, bg);
  int w = tft.textWidth(s);
  tft.drawString(s, (SCR_W - w) / 2, y);
}

void drawHeader(const char* title, const char* rightText) {
  tft.fillRect(0, 0, SCR_W, HDR_H, C_HEADER);
  tft.setTextSize(1);
  tft.setTextColor(C_WHITE, C_HEADER);
  tft.drawString(title, 8, 6);
  if (rightText) {
    int rw = tft.textWidth(rightText);
    tft.drawString(rightText, SCR_W - rw - 8, 6);
  }
}

void drawDivider(int y) {
  tft.drawFastHLine(4, y, SCR_W - 8, C_DIVIDER);
}

void btn(const char* label, int x, int y, int w, int h, uint16_t bg, uint16_t fg, int sz) {
  tft.fillRoundRect(x, y, w, h, 4, bg);
  tft.drawRoundRect(x, y, w, h, 4, C_DIVIDER);
  tft.setTextSize(sz);
  tft.setTextColor(fg, bg);
  int lw = tft.textWidth(label);
  tft.drawString(label, x + (w - lw) / 2, y + (h - 10) / 2);
}

bool btnHit(int x, int y, int w, int h, int tx, int ty) {
  return (tx >= x && tx <= x + w && ty >= y && ty <= y + h);
}

// ── Сортировка пузырьком по changes (убывание) ──
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

// ── Поиск ID в буфере ──
int findId(unsigned long id) {
  for (int i = 0; i < scanCount; i++)
    if (scanBuf[i].id == id) return i;
  return -1;
}

// ═══════════════════════════════════════════════════════════════
//  CAN-ЯДРО (MCP2515 + FIFO)
// ═══════════════════════════════════════════════════════════════

bool initCAN() {
  pinMode(CAN_CS, OUTPUT);
  digitalWrite(CAN_CS, HIGH);
  delay(50);
  if (CAN0.begin(MCP_ANY, CAN_500KBPS, MCP_8MHZ) != CAN_OK) {
    return false;
  }
  CAN0.setMode(MCP_LISTENONLY);
  return true;
}

// Мягкая проверка — можно звать из loop
bool canOnline() {
  // Быстро дёргаем регистр статуса через SPI
  return (CAN0.checkReceive() != CAN_GETFLT);
}

// ═══════════════════════════════════════════════════════════════
//  ДЕКОДЕР — таблица HondaCAN + byte parsing
// ═══════════════════════════════════════════════════════════════

void decodeMessage(unsigned long id, uint8_t* data, int len) {
  decodedCount = 0;
  
  switch (id) {
    case 0x17C: { // POWERTRAIN_DATA — RPM, pedal
      // RPM = data[2] (MSB) << 8 | data[3] (LSB) — 100Hz
      if (len >= 4) {
        uint16_t rpm = ((uint16_t)data[2] << 8) | data[3];
        decodedVals[decodedCount++] = {"RPM", (float)rpm, "об/мин"};
        decodedVals[decodedCount++] = {"Газ", (float)data[0], "%"};
      }
      break;
    }
    case 0x309: // CAR_SPEED — скорость км/ч
      if (len >= 1) {
        decodedVals[decodedCount++] = {"Скорость", (float)data[0], "км/ч"};
      }
      break;
    case 0x324: { // ENGINE_DATA_3 — температура ОЖ
      if (len >= 2) {
        int coolant = data[0] - 40; // byte[0] - 40
        decodedVals[decodedCount++] = {"ОЖ", (float)coolant, "°C"};
        decodedVals[decodedCount++] = {"Впуск", (float)(data[1] - 40), "°C"};
      }
      break;
    }
    case 0x1A6: { // SCM_FEEDBACK_1 — топливо
      if (len >= 6) {
        // fuel level = byte 5 (0-255) → 0-100%
        int fuel = map(data[5], 0, 255, 0, 100);
        decodedVals[decodedCount++] = {"Топливо", (float)fuel, "%"};
        decodedVals[decodedCount++] = {"Cruise", data[0] & 0x80 ? 1.0f : 0, ""};
      }
      break;
    }
    case 0x191: { // GEARBOX — PRNDSL
      if (len >= 1) {
        const char* gears[] = {"P", "R", "N", "D", "2", "1", "L", "S"};
        int g = data[0] & 0x07;
        decodedVals[decodedCount++] = {"Передача", (float)g, (g < 8) ? gears[g] : "?"};
      }
      break;
    }
    case 0x1A4: // VSA_STATUS — тормозное давление
      if (len >= 1) {
        decodedVals[decodedCount++] = {"Тормоза", (float)map(data[0], 0, 255, 0, 100), "%"};
      }
      break;
    case 0x158: { // ENGINE_DATA_2 — скорость
      if (len >= 3) {
        uint16_t spd = ((uint16_t)data[1] << 8) | data[2];
        decodedVals[decodedCount++] = {"Скорость", (float)spd * 0.01f, "км/ч"};
      }
      break;
    }
    case 0x13C: // ENGINE_DATA_1 — газ
      if (len >= 1) {
        decodedVals[decodedCount++] = {"Педаль", (float)data[0] * 100.0f / 255.0f, "%"};
      }
      break;
    case 0x18E: // KINEMATICS — ускорения
      if (len >= 4) {
        float lat = (float)(int8_t)data[0] * 0.01f;
        float lon = (float)(int8_t)data[1] * 0.01f;
        decodedVals[decodedCount++] = {"Lat accel", lat, "g"};
        decodedVals[decodedCount++] = {"Lon accel", lon, "g"};
      }
      break;
    case 0x294: // SCM_FEEDBACK_2 — поворотники, одометр
      if (len >= 4) {
        if (data[0] & 0x01) decodedVals[decodedCount++] = {"Поворот", 1, "←Левый"};
        if (data[0] & 0x02) decodedVals[decodedCount++] = {"Поворот", 1, "→Правый"};
        uint32_t odo = ((uint32_t)data[1] << 16) | ((uint32_t)data[2] << 8) | data[3];
        decodedVals[decodedCount++] = {"Пробег", (float)odo, "км"};
      }
      break;
    case 0x255: // ROUGH_WHEEL_SPEED
      if (len >= 8) {
        decodedVals[decodedCount++] = {"FL", (float)((data[0]<<8)|data[1]), "км/ч"};
        decodedVals[decodedCount++] = {"FR", (float)((data[2]<<8)|data[3]), "км/ч"};
        decodedVals[decodedCount++] = {"RL", (float)((data[4]<<8)|data[5]), "км/ч"};
        decodedVals[decodedCount++] = {"RR", (float)((data[6]<<8)|data[7]), "км/ч"};
      }
      break;
    default: {
      // Неизвестный ID — показываем сырые байты
      for (int i = 0; i < len && i < 8; i++) {
        if (decodedCount < 4) {
          char lbl[4]; sprintf(lbl, "B%d", i);
          decodedVals[decodedCount++] = {lbl, (float)data[i], ""};
        }
      }
      break;
    }
  }
}

// ═══════════════════════════════════════════════════════════════
//  РЕЖИМ 1: МЕНЮ
// ═══════════════════════════════════════════════════════════════

#define BTN_MARGIN 6
#define BTN_GAP    6
#define MENU_COLS  2
int btnW, btnH;

void calcMenuLayout() {
  btnW = (SCR_W - BTN_MARGIN * 2 - BTN_GAP) / MENU_COLS;
  btnH = 50;
}

void drawMenu() {
  calcMenuLayout();
  drawHeader("CAN-MULTITOOL ⚡", "v2.0");
  
  const char* menuItems[] = {
    "🔍 Сканер ID", "📊 Монитор",
    "🏎️ Спидометр", "🔬 Датчики",
    "🔎 Поиск блоков", "📝 Логгер",
    "📡 OBD2", "🧪 Тест ID"
  };
  const Mode menuModes[] = {
    MODE_SCANNING, MODE_MONITOR_RAW,
    MODE_SPEEDO, MODE_SENSORS,
    MODE_BLOCKFIND, MODE_LOGGER,
    MODE_OBD2, MODE_PROBE
  };
  int items = 8;
  int startIdx = menuPage * MENU_PER_PAGE * MENU_COLS;
  
  int y = HDR_H + 8;
  int visible = 0;
  for (int i = startIdx; i < items && visible < MENU_PER_PAGE * MENU_COLS; i++) {
    int col = visible % MENU_COLS;
    int row = visible / MENU_COLS;
    int bx = BTN_MARGIN + col * (btnW + BTN_GAP);
    int by = y + row * (btnH + BTN_GAP);
    
    uint16_t bg = C_BTN_BG;
    if (i == 0 && mode != MODE_MENU) bg = C_ACCENT; // подсветка активного
    btn(menuItems[i], bx, by, btnW, btnH, bg, C_WHITE, 1);
    visible++;
  }
  
  // Навигация
  int totalPages = (items + MENU_PER_PAGE * MENU_COLS - 1) / (MENU_PER_PAGE * MENU_COLS);
  if (totalPages > 1) {
    char pg[10]; sprintf(pg, "%d/%d", menuPage + 1, totalPages);
    tft.setTextSize(1);
    tft.setTextColor(C_GREY, C_BG);
    tft.drawString(pg, SCR_W / 2 - 10, 220);
    btn("<", 10, 215, 30, 20, C_LIST_BG, C_WHITE, 1);
    btn(">", SCR_W - 40, 215, 30, 20, C_LIST_BG, C_WHITE, 1);
  }
  
  // Статус CAN
  tft.fillCircle(SCR_W - 14, 14, 4, canOnline() ? C_GREEN : C_RED);
}

void handleMenuTouch(int tx, int ty) {
  calcMenuLayout();
  const Mode menuModes[] = {
    MODE_SCANNING, MODE_MONITOR_RAW,
    MODE_SPEEDO, MODE_SENSORS,
    MODE_BLOCKFIND, MODE_LOGGER,
    MODE_OBD2, MODE_PROBE
  };
  int items = 8;
  int startIdx = menuPage * MENU_PER_PAGE * MENU_COLS;
  
  int y = HDR_H + 8;
  int visible = 0;
  for (int i = startIdx; i < items && visible < MENU_PER_PAGE * MENU_COLS; i++) {
    int col = visible % MENU_COLS;
    int row = visible / MENU_COLS;
    int bx = BTN_MARGIN + col * (btnW + BTN_GAP);
    int by = y + row * (btnH + BTN_GAP);
    if (btnHit(bx, by, btnW, btnH, tx, ty)) {
      mode = menuModes[i];
      redrawNeeded = true;
      if (mode == MODE_SCANNING) {
        // Запускаем сканирование
        scanCount = 0; sortedCount = 0;
        scanStartMs = millis();
      }
      return;
    }
    visible++;
  }
  
  // Навигация
  int totalPages = (items + MENU_PER_PAGE * MENU_COLS - 1) / (MENU_PER_PAGE * MENU_COLS);
  if (totalPages > 1) {
    if (btnHit(10, 215, 30, 20, tx, ty) && menuPage > 0) { menuPage--; redrawNeeded = true; }
    if (btnHit(SCR_W - 40, 215, 30, 20, tx, ty) && menuPage < totalPages - 1) { menuPage++; redrawNeeded = true; }
  }
}

// ═══════════════════════════════════════════════════════════════
//  РЕЖИМ 2: СКАНЕР ID (порт из v1.2)
// ═══════════════════════════════════════════════════════════════

void drawScanner() {
  char tmp[48];
  sprintf(tmp, "SCANNING: %lds", (scanDurationMs - (millis() - scanStartMs)) / 1000);
  drawHeader(tmp, NULL);
  btn("STOP", 100, 70, 120, 32, TFT_BLACK, C_RED, 2);
  
  // Progress bar
  int pct = ((millis() - scanStartMs) * 300) / scanDurationMs;
  if (pct > 300) pct = 300;
  tft.fillRect(10, 120, 300, 20, C_LIST_BG);
  tft.drawRect(10, 120, 300, 20, C_WHITE);
  if (pct > 0) tft.fillRect(10, 120, pct, 20, C_ACCENT);
  
  // Pulse dot
  tft.fillCircle(18, 50, 4, pulseState ? C_GREEN : C_HEADER);
  tft.setTextColor(C_GREY, C_BG);
  tft.drawString("Listening for CAN IDs...", 30, 46);
  
  // ID counter
  sprintf(tmp, "Found: %d IDs", scanCount);
  tft.setTextSize(1);
  tft.setTextColor(C_WHITE, C_BG);
  tft.drawString(tmp, 10, 150);
}

void handleScannerTouch(int tx, int ty) {
  if (btnHit(100, 70, 120, 32, tx, ty)) {
    mode = MODE_MENU;
    redrawNeeded = true;
  }
}

// ═══════════════════════════════════════════════════════════════
//  РЕЖИМ 3: LIST (порт из v1.2)
// ═══════════════════════════════════════════════════════════════

void drawList() {
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
    btn("BACK", 110, 180, 100, 28, TFT_BLACK, C_ACCENT, 1);
    return;
  }
  
  int ry = HDR_H + 6;
  for (int i = 0; i < itemsOnPage; i++) {
    int idx = start + i;
    int si = sortedIdx[idx];
    ScanEntry* e = &scanBuf[si];
    int rowH = 31;
    uint16_t rowBg = (i % 2 == 0) ? C_BG : C_LIST_BG;
    tft.fillRoundRect(6, ry, 308, rowH, 4, rowBg);
    
    char tmp[24];
    sprintf(tmp, "0x%03lX", e->id);
    tft.setTextSize(2);
    tft.setTextColor(C_ACCENT, rowBg);
    tft.drawString(tmp, 12, ry + 6);
    
    tft.setTextSize(1);
    tft.setTextColor(C_GREY, rowBg);
    sprintf(tmp, "cnt:%lu chg:%lu", e->count, e->changes);
    tft.drawString(tmp, 120, ry + 8);
    
    // Raw data snippet
    char hex[18];
    hex[0] = 0;
    for (int b = 0; b < e->firstSeen && b < 4; b++) {
      sprintf(hex + strlen(hex), "%02X ", e->lastData[b]);
    }
    tft.drawString(hex, 120, ry + 20);
    ry += rowH + 3;
  }
  
  // Навигация
  ry = 210;
  if (listPage > 0) btn("<", 10, ry, 40, 24, C_LIST_BG, C_WHITE, 2);
  if (listPage < pages - 1) btn(">", 270, ry, 40, 24, C_LIST_BG, C_WHITE, 2);
  btn("MENU", 100, ry, 120, 24, C_BTN_BG, C_WHITE, 1);
}

void handleListTouch(int tx, int ty) {
  int pages = sortedCount > 0 ? (sortedCount + PER_PAGE - 1) / PER_PAGE : 1;
  int start = listPage * PER_PAGE;
  int itemsOnPage = sortedCount - start;
  if (itemsOnPage > PER_PAGE) itemsOnPage = PER_PAGE;
  
  int ry = HDR_H + 6;
  for (int i = 0; i < itemsOnPage; i++) {
    int rowH = 31;
    if (btnHit(6, ry, 308, rowH, tx, ty)) {
      int si = sortedIdx[start + i];
      monitorId = scanBuf[si].id;
      monitorHasData = false;
      // Определяем какой монитор: если ID известен — DECODE, иначе RAW
      bool known = false;
      switch (monitorId) {
        case 0x17C: case 0x309: case 0x324: case 0x1A6:
        case 0x191: case 0x1A4: case 0x158: case 0x13C:
        case 0x18E: case 0x294: case 0x255:
          known = true; break;
      }
      mode = known ? MODE_MONITOR_DECODE : MODE_MONITOR_RAW;
      redrawNeeded = true;
      return;
    }
    ry += rowH + 3;
  }
  
  ry = 210;
  if (btnHit(10, ry, 40, 24, tx, ty) && listPage > 0) { listPage--; redrawNeeded = true; }
  if (btnHit(270, ry, 40, 24, tx, ty) && listPage < pages - 1) { listPage++; redrawNeeded = true; }
  if (btnHit(100, ry, 120, 24, tx, ty)) { mode = MODE_MENU; redrawNeeded = true; }
}

// ═══════════════════════════════════════════════════════════════
//  РЕЖИМ 4: MONITOR RAW (как v1.2 — бар)
// ═══════════════════════════════════════════════════════════════

#define MON_BAR_X    20
#define MON_BAR_Y    36
#define MON_BAR_W    60
#define MON_BAR_H    160
#define MON_PCT_X    (MON_BAR_X + MON_BAR_W + 16)
#define MON_PCT_Y    (MON_BAR_Y + 30)
#define MON_VAL_Y    (MON_PCT_Y + 48)

void drawMonitorRaw() {
  char tmp[48];
  sprintf(tmp, "MONITOR: 0x%03lX", monitorId);
  drawHeader(tmp, NULL);
  btn("BACK", 248, 3, 66, 18, TFT_BLACK, C_RED, 1);
  
  // Рамка бара
  tft.drawRoundRect(MON_BAR_X, MON_BAR_Y, MON_BAR_W, MON_BAR_H, 4, C_DIVIDER);
  int barFill = map(brightnessVal, 0, 100, 2, MON_BAR_H - 4);
  int barY = MON_BAR_Y + MON_BAR_H - 2 - barFill;
  tft.fillRoundRect(MON_BAR_X + 2, barY, MON_BAR_W - 4, barFill, 2, C_ACCENT);
  
  // Процент крупно
  tft.setTextSize(5);
  tft.setTextColor(C_WHITE, C_BG);
  sprintf(tmp, "%3d%%", brightnessVal);
  tft.drawString(tmp, MON_PCT_X, MON_PCT_Y);
  
  // Raw value
  tft.setTextSize(3);
  tft.setTextColor(C_ACCENT, C_BG);
  sprintf(tmp, "%d", displayRawVal);
  tft.drawString(tmp, MON_PCT_X, MON_VAL_Y);
  
  // Байты под баром
  drawDivider(206);
  tft.setTextSize(1);
  tft.setTextColor(C_GREY, C_BG);
  if (monitorHasData) {
    char hex[30]; hex[0] = 0;
    for (int i = 0; i < monitorLen; i++) {
      sprintf(hex + strlen(hex), " %02X", monitorBytes[i]);
    }
    sprintf(tmp, "DATA:%s", hex);
    tft.drawString(tmp, 10, 214);
  }
}

void updateMonitorRawValue() {
  tft.fillRect(MON_BAR_X + 2, MON_BAR_Y + 2, MON_BAR_W - 4, MON_BAR_H - 4, C_BG);
  int barFill = map(brightnessVal, 0, 100, 2, MON_BAR_H - 4);
  int barY = MON_BAR_Y + MON_BAR_H - 2 - barFill;
  tft.fillRoundRect(MON_BAR_X + 2, barY, MON_BAR_W - 4, barFill, 2, C_ACCENT);
  
  tft.setTextSize(5);
  char tmp[10];
  sprintf(tmp, "%3d%%", brightnessVal);
  tft.fillRect(MON_PCT_X - 2, MON_PCT_Y - 2, 130, 34, C_BG);
  tft.setTextColor(C_WHITE, C_BG);
  tft.drawString(tmp, MON_PCT_X, MON_PCT_Y);
  
  tft.setTextSize(3);
  sprintf(tmp, "%d", displayRawVal);
  tft.fillRect(MON_PCT_X - 2, MON_VAL_Y - 2, 80, 26, C_BG);
  tft.setTextColor(C_ACCENT, C_BG);
  tft.drawString(tmp, MON_PCT_X, MON_VAL_Y);
}

void handleMonitorRawTouch(int tx, int ty) {
  if (btnHit(248, 3, 66, 18, tx, ty)) { mode = MODE_LIST; redrawNeeded = true; }
}

// ═══════════════════════════════════════════════════════════════
//  РЕЖИМ 5: MONITOR DECODE
// ═══════════════════════════════════════════════════════════════

void drawMonitorDecode() {
  char tmp[48];
  sprintf(tmp, "DECODE: 0x%03lX", monitorId);
  drawHeader(tmp, NULL);
  btn("BACK", 248, 3, 66, 18, TFT_BLACK, C_RED, 1);
  
  if (!monitorHasData) {
    centerText("Waiting for CAN data...", 100, C_GREY, C_BG, 2);
    return;
  }
  
  // Сырые байты сверху
  tft.setTextSize(1);
  tft.setTextColor(C_GREY, C_BG);
  char hex[30]; hex[0] = 0;
  for (int i = 0; i < monitorLen; i++) {
    sprintf(hex + strlen(hex), " %02X", monitorBytes[i]);
  }
  sprintf(tmp, "RAW: %s", hex);
  tft.drawString(tmp, 8, HDR_H + 6);
  
  drawDivider(HDR_H + 20);
  
  // Расшифрованные значения — крупно
  decodeMessage(monitorId, monitorBytes, monitorLen);
  int vy = HDR_H + 34;
  for (int i = 0; i < decodedCount && i < 4; i++) {
    char line[32];
    if (decodedVals[i].value == (int)decodedVals[i].value) {
      sprintf(line, "%s: %d %s", decodedVals[i].label, (int)decodedVals[i].value, decodedVals[i].unit);
    } else {
      sprintf(line, "%s: %.1f %s", decodedVals[i].label, decodedVals[i].value, decodedVals[i].unit);
    }
    tft.setTextSize(2);
    tft.setTextColor(C_WHITE, C_BG);
    tft.drawString(line, 12, vy);
    vy += 36;
  }
}

void updateMonitorDecodeValue() {
  // Перерисовка только данных
  if (!monitorHasData) return;
  
  char hex[30]; hex[0] = 0;
  for (int i = 0; i < monitorLen; i++) {
    sprintf(hex + strlen(hex), " %02X", monitorBytes[i]);
  }
  char tmp[48]; sprintf(tmp, "RAW: %s", hex);
  
  tft.fillRect(8, HDR_H + 6, 300, 14, C_BG);
  tft.setTextSize(1);
  tft.setTextColor(C_GREY, C_BG);
  tft.drawString(tmp, 8, HDR_H + 6);
  
  // Значения
  decodeMessage(monitorId, monitorBytes, monitorLen);
  int vy = HDR_H + 34;
  for (int i = 0; i < decodedCount && i < 4; i++) {
    char line[32];
    if (decodedVals[i].value == (int)decodedVals[i].value) {
      sprintf(line, "%s: %d %s", decodedVals[i].label, (int)decodedVals[i].value, decodedVals[i].unit);
    } else {
      sprintf(line, "%s: %.1f %s", decodedVals[i].label, decodedVals[i].value, decodedVals[i].unit);
    }
    tft.fillRect(12, vy, 296, 28, C_BG);
    tft.setTextSize(2);
    tft.setTextColor(C_WHITE, C_BG);
    tft.drawString(line, 12, vy);
    vy += 36;
  }
}

void handleMonitorDecodeTouch(int tx, int ty) {
  if (btnHit(248, 3, 66, 18, tx, ty)) { mode = MODE_LIST; redrawNeeded = true; }
}

// ═══════════════════════════════════════════════════════════════
//  РЕЖИМ 6: СПИДОМЕТР
// ═══════════════════════════════════════════════════════════════

uint16_t speedoValue = 0;
uint16_t rpmValue = 0;
String speedoMode = "speed"; // speed, rpm, both

void drawSpeedo() {
  drawHeader("🏎️ SPEDOMETER", NULL);
  btn("MENU", 248, 3, 66, 18, TFT_BLACK, C_RED, 1);
  btn("RPM", 10, 3, 40, 18, C_BTN_BG, C_WHITE, 1);
  btn("SPD", 55, 3, 40, 18, C_BTN_BG, C_WHITE, 1);
  
  char tmp[16];
  tft.setTextSize(6);
  if (speedoMode == "speed") {
    tft.setTextColor(C_ACCENT, C_BG);
    sprintf(tmp, "%d", speedoValue);
    tft.drawString(tmp, 20, 80);
    tft.setTextSize(2);
    tft.setTextColor(C_GREY, C_BG);
    tft.drawString("km/h", 40, 150);
  } else if (speedoMode == "rpm") {
    tft.setTextColor(C_GREEN, C_BG);
    sprintf(tmp, "%d", rpmValue);
    tft.drawString(tmp, 20, 80);
    tft.setTextSize(2);
    tft.setTextColor(C_GREY, C_BG);
    tft.drawString("RPM x100", 40, 150);
  } else if (speedoMode == "both") {
    tft.setTextSize(4);
    tft.setTextColor(C_ACCENT, C_BG);
    sprintf(tmp, "%3d", speedoValue);
    tft.drawString(tmp, 20, 60);
    tft.setTextSize(1);
    tft.setTextColor(C_GREY, C_BG);
    tft.drawString("km/h", 160, 65);
    
    tft.setTextSize(4);
    tft.setTextColor(C_GREEN, C_BG);
    sprintf(tmp, "%4d", rpmValue);
    tft.drawString(tmp, 20, 120);
    tft.setTextSize(1);
    tft.setTextColor(C_GREY, C_BG);
    tft.drawString("RPM", 160, 125);
  }
  
  // CAN status indicator
  tft.fillCircle(SCR_W - 14, 14, 4, canOnline() ? C_GREEN : C_RED);
}

void updateSpeedoValue() {
  char tmp[16];
  if (speedoMode == "speed") {
    tft.fillRect(10, 40, 300, 160, C_BG);
    tft.setTextSize(6);
    tft.setTextColor(C_ACCENT, C_BG);
    sprintf(tmp, "%d", speedoValue);
    tft.drawString(tmp, 20, 80);
    tft.setTextSize(2);
    tft.setTextColor(C_GREY, C_BG);
    tft.drawString("km/h", 40, 150);
  } else if (speedoMode == "rpm") {
    tft.fillRect(10, 40, 300, 160, C_BG);
    tft.setTextSize(6);
    tft.setTextColor(C_GREEN, C_BG);
    sprintf(tmp, "%d", rpmValue);
    tft.drawString(tmp, 20, 80);
    tft.setTextSize(2);
    tft.setTextColor(C_GREY, C_BG);
    tft.drawString("RPM x100", 40, 150);
  } else if (speedoMode == "both") {
    tft.fillRect(10, 40, 300, 160, C_BG);
    tft.setTextSize(4);
    tft.setTextColor(C_ACCENT, C_BG);
    sprintf(tmp, "%3d", speedoValue);
    tft.drawString(tmp, 20, 60);
    tft.setTextSize(1);
    tft.setTextColor(C_GREY, C_BG);
    tft.drawString("km/h", 160, 65);
    tft.setTextSize(4);
    tft.setTextColor(C_GREEN, C_BG);
    sprintf(tmp, "%4d", rpmValue);
    tft.drawString(tmp, 20, 120);
    tft.setTextSize(1);
    tft.setTextColor(C_GREY, C_BG);
    tft.drawString("RPM", 160, 125);
  }
}

void handleSpeedoTouch(int tx, int ty) {
  if (btnHit(248, 3, 66, 18, tx, ty)) { mode = MODE_MENU; redrawNeeded = true; }
  if (btnHit(10, 3, 40, 18, tx, ty)) { speedoMode = "rpm"; redrawNeeded = true; }
  if (btnHit(55, 3, 40, 18, tx, ty)) { speedoMode = "speed"; redrawNeeded = true; }
}

// ═══════════════════════════════════════════════════════════════
//  РЕЖИМ 7: SENSORS (сетка датчиков — заглушка)
// ═══════════════════════════════════════════════════════════════

String sensorLabels[] = {"Speed", "RPM", "Temp", "Fuel", "Throttle", "Voltage"};
String sensorVals[] = {"--", "--", "--", "--", "--", "--"};
String sensorUnits[] = {"km/h", "x100", "°C", "%", "%", "V"};

void drawSensors() {
  drawHeader("🔬 ENGINE SENSORS", NULL);
  btn("MENU", 248, 58, 66, 18, TFT_BLACK, C_RED, 1);
  
  int cols = 2;
  int cellW = 140;
  int cellH = 50;
  int startX = 10;
  int startY = HDR_H + 10;
  
  for (int i = 0; i < 6; i++) {
    int col = i % cols;
    int row = i / cols;
    int cx = startX + col * (cellW + 8);
    int cy = startY + row * (cellH + 6);
    
    tft.fillRoundRect(cx, cy, cellW, cellH, 4, C_LIST_BG);
    tft.drawRoundRect(cx, cy, cellW, cellH, 4, C_DIVIDER);
    
    tft.setTextSize(1);
    tft.setTextColor(C_GREY, C_LIST_BG);
    tft.drawString(sensorLabels[i], cx + 6, cy + 4);
    
    char val[12];
    sprintf(val, "%s %s", sensorVals[i].c_str(), sensorUnits[i].c_str());
    tft.setTextSize(2);
    tft.setTextColor(C_WHITE, C_LIST_BG);
    tft.drawString(val, cx + 6, cy + 24);
  }
}

// ═══════════════════════════════════════════════════════════════
//  CAN-ОБРАБОТКА (основной вызов из loop)
// ═══════════════════════════════════════════════════════════════

void processCanData() {
  if (fifoHead == fifoTail) return;
  
  while (fifoHead != fifoTail) {
    CanMsg msg = fifo[fifoTail];
    fifoTail = (fifoTail + 1) % FIFO_SIZE;
    
    // Режим сканирования — собираем ID
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
    
    // Мониторинг выбранного ID
    if ((mode == MODE_MONITOR_RAW || mode == MODE_MONITOR_DECODE || mode == MODE_SPEEDO) && msg.id == monitorId) {
      monitorHasData = true;
      monitorLen = msg.len;
      memcpy(monitorBytes, msg.data, 8);
      
      // Для RAW монитора
      displayRawVal = msg.data[0];
      brightnessVal = (msg.data[0] > 100) ? map(msg.data[0], 0, 255, 0, 100) : msg.data[0];
      
      // Для спидометра — парсим скорость (0x309) и RPM (0x17C)
      if (mode == MODE_SPEEDO) {
        if (msg.id == 0x309 && msg.len >= 1) speedoValue = msg.data[0];
        if (msg.id == 0x17C && msg.len >= 4) rpmValue = ((uint16_t)msg.data[2] << 8) | msg.data[3];
      }
      
      unsigned long n = millis();
      if (n - lastMonitorUpdate >= MONITOR_UPDATE_MS) {
        lastMonitorUpdate = n;
        valueUpdateNeeded = true;
      }
    }
    
    // Спидометр — слушаем оба ID
    if (mode == MODE_SPEEDO) {
      if (msg.id == 0x309 && msg.len >= 1) speedoValue = msg.data[0];
      if (msg.id == 0x17C && msg.len >= 4) rpmValue = ((uint16_t)msg.data[2] << 8) | msg.data[3];
    }
  }
}

// ═══════════════════════════════════════════════════════════════
//  ДИСПЕТЧЕР ЭКРАНА
// ═══════════════════════════════════════════════════════════════

void drawScreen() {
  switch (mode) {
    case MODE_MENU:          drawMenu(); break;
    case MODE_SCANNING:      drawScanner(); break;
    case MODE_LIST:          drawList(); break;
    case MODE_MONITOR_RAW:   drawMonitorRaw(); break;
    case MODE_MONITOR_DECODE: drawMonitorDecode(); break;
    case MODE_SPEEDO:        drawSpeedo(); break;
    case MODE_SENSORS:       drawSensors(); break;
    default:                 drawMenu(); break;
  }
}

void handleTouch() {
  int tx, ty;
  if (!tft.getTouch(&tx, &ty, 4)) return;
  
  // wait release
  delay(60);
  while (tft.getTouch(&tx, &ty, 4)) delay(10);
  
  switch (mode) {
    case MODE_MENU:          handleMenuTouch(tx, ty); break;
    case MODE_SCANNING:      handleScannerTouch(tx, ty); break;
    case MODE_LIST:          handleListTouch(tx, ty); break;
    case MODE_MONITOR_RAW:   handleMonitorRawTouch(tx, ty); break;
    case MODE_MONITOR_DECODE: handleMonitorDecodeTouch(tx, ty); break;
    case MODE_SPEEDO:        handleSpeedoTouch(tx, ty); break;
    default:                 handleMenuTouch(tx, ty); break;
  }
}

// ═══════════════════════════════════════════════════════════════
//  SPI КОЛЛБЭК (из прерывания MCP2515)
// ═══════════════════════════════════════════════════════════════

// Вызывается из loop() через checkReceive
void drainMCP() {
  if (spiBusy) return;
  spiBusy = true;
  
  long unsigned int id;
  unsigned char len;
  unsigned char buf[8];
  unsigned char ext;
  
  while (CAN0.checkReceive() == CAN_MSGAVAIL &&
         ((fifoHead + 1) % FIFO_SIZE) != fifoTail) {
    if (CAN0.readMsgBuf(&id, &len, buf) == CAN_OK) {
      int nh = (fifoHead + 1) % FIFO_SIZE;
      if (nh != fifoTail) {
        fifo[fifoHead].id = id;
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
  
  // TFT init
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(C_BG);
  tft.setTextFont(2);
  
  // Touch init
  uint16_t calData[5] = { 275, 3526, 268, 3447, 1 };
  tft.setTouch(calData);
  
  // CAN init
  if (!initCAN()) {
    tft.setTextSize(2);
    tft.setTextColor(C_RED, C_BG);
    centerText("CAN ERROR!", 80, C_RED, C_BG, 2);
    centerText("Check wiring", 110, C_GREY, C_BG, 1);
  }
  
  redrawNeeded = true;
}

// ═══════════════════════════════════════════════════════════════
//  LOOP
// ═══════════════════════════════════════════════════════════════

void loop() {
  unsigned long now = millis();
  
  // 1. Дренаж MCP2515 → FIFO
  drainMCP();
  
  // 2. Обработка FIFO
  processCanData();
  
  // 3. Таймер сканирования
  if (mode == MODE_SCANNING && (now - scanStartMs >= scanDurationMs)) {
    sortByChanges();
    listPage = 0;
    mode = MODE_LIST;
    redrawNeeded = true;
    delay(20);
    return;
  }
  
  // 4. Pulse dot в сканере
  if (mode == MODE_SCANNING) {
    if (now - lastPulseMs >= 500) {
      lastPulseMs = now;
      pulseState = !pulseState;
      tft.fillCircle(18, 50, 4, pulseState ? C_GREEN : C_HEADER);
    }
    // Прогресс-бар
    unsigned long remain = (scanStartMs + scanDurationMs > now)
      ? (scanStartMs + scanDurationMs - now) / 1000 : 0;
    int pct = ((now - scanStartMs) * 300) / scanDurationMs;
    if (pct > 300) pct = 300;
    tft.fillRect(10, 120, 300, 20, C_LIST_BG);
    tft.drawRect(10, 120, 300, 20, C_WHITE);
    if (pct > 0) tft.fillRect(10, 120, pct, 20, C_ACCENT);
    
    char tmp[24];
    sprintf(tmp, "Listening...  %2lus", (unsigned long)remain);
    tft.setTextSize(1);
    tft.setTextColor(C_WHITE, C_BG);
    tft.fillRect(10, 145, 300, 10, C_BG);
    tft.drawString(tmp, 10, 146);
    
    sprintf(tmp, "Found: %d IDs", scanCount);
    tft.drawString(tmp, 10, 158);
  }
  
  // 5. Частичное обновление монитора/спидометра
  if (valueUpdateNeeded) {
    valueUpdateNeeded = false;
    switch (mode) {
      case MODE_MONITOR_RAW:   updateMonitorRawValue(); break;
      case MODE_MONITOR_DECODE: updateMonitorDecodeValue(); break;
      case MODE_SPEEDO:        updateSpeedoValue(); break;
      default: break;
    }
  }
  
  // 6. Полная перерисовка
  if (redrawNeeded) {
    redrawNeeded = false;
    tft.fillScreen(C_BG);
    drawScreen();
  }
  
  // 7. Обработка тача
  handleTouch();
  
  // 8. Yield каждые 5ms
  if (now - lastYieldMs >= 5) {
    lastYieldMs = now;
    yield();
  }
}

// ═══════════════════════════════════════════════════════════════
//  END OF CAN-MULTITOOL.INO
// ═══════════════════════════════════════════════════════════════
