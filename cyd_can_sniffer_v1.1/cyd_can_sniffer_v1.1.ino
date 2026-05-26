/**
 * ESP32-2432S028 (CYD) — CAN ID Hunter
 * v1.1 — Watchdog fix + stack overflow fix
 *
 * Подключение MCP2515 (требуется пайка):
 *   SPI:     ножки ESP32 (SCK=18, MOSI=23, MISO=19)
 *   5V/GND:  площадки S3/S1 сзади платы
 *   CS:      разъём P3, пин 3 (GPIO22) — без пайки
 *   CAN_H/L: OBD2 пины 6/14
 *
 * Пайка не нужна только для CS — провод мама-мама в P3.
 */
#include <SPI.h>
#include <mcp2515_can.h>
#include <TFT_eSPI.h>

#define TOUCH_CAL { 320, 3424, 356, 3385, 1 }
#define PIN_CAN_CS    22
#define CAN_SPEED     CAN_500KBPS

mcp2515_can CAN(PIN_CAN_CS);
TFT_eSPI tft;
uint16_t tcal[5] = TOUCH_CAL;

// ── Цветовая схема ──
#define C_BG        TFT_BLACK
#define C_HEADER    0x1A3C      // тёмно-синий морской
#define C_DIVIDER   0x2945      // разделитель
#define C_LIST_BG   0x0841      // фон строк списка
#define C_ACCENT    TFT_CYAN
#define C_WARM      TFT_YELLOW
#define C_GREEN     TFT_GREEN
#define C_RED       TFT_RED
#define C_GREY      0x7BEF
#define C_BLUE      0x33DF      // ярко-голубой для кнопок
#define C_ORANGE    0xFDA0      // оранжевый для анимации
#define C_WHITE     TFT_WHITE

#define SCR_W    320
#define SCR_H    240
#define HDR_H    24
#define TOUCH_D  250

enum Mode { MODE_IDLE, MODE_SCANNING, MODE_LIST, MODE_MONITOR };
Mode mode = MODE_IDLE;

#define MAX_SCAN_IDS 80
struct ScanEntry {
  uint32_t id; uint32_t count; uint32_t changes;
  byte prev[8]; byte prevLen; bool active;
};
ScanEntry scanBuf[MAX_SCAN_IDS];
int scanCount = 0;
int sortedIdx[MAX_SCAN_IDS];
int sortedCount = 0;

bool canOk = false;
unsigned long scanStartMs = 0;
unsigned long scanDurationMs = 30000;
uint32_t totalPkts = 0;
unsigned long lastTimerMs = 0;

uint32_t monitorId = 0;
int brightnessVal = 0;

// ── Кольцевой FIFO для CAN-сообщений (SPI drain → неторопливая обработка) ──
#define CAN_QUEUE_SIZE 512
struct CanPacket {
  uint32_t id; byte len; byte data[8];
};
CanPacket canQueue[CAN_QUEUE_SIZE];
volatile int canHead = 0;
volatile int canTail = 0;

bool canPush(uint32_t id, byte len, byte *d) {
  int nxt = (canHead + 1) % CAN_QUEUE_SIZE;
  if (nxt == canTail) return false;
  canQueue[canHead].id = id; canQueue[canHead].len = len;
  memcpy(canQueue[canHead].data, d, len);
  canHead = nxt; return true;
}
bool canPop(uint32_t *id, byte *len, byte *d) {
  if (canTail == canHead) return false;
  *id = canQueue[canTail].id; *len = canQueue[canTail].len;
  memcpy(d, canQueue[canTail].data, canQueue[canTail].len);
  canTail = (canTail + 1) % CAN_QUEUE_SIZE; return true;
}

#define PER_PAGE 5
int listPage = 0;
bool redrawNeeded = true;
bool pulseState = false;
unsigned long lastPulseMs = 0;
unsigned long lastYieldMs = 0;

// ── helpers ──
void centerText(const char *s, int y, int fg, int bg, uint8_t sz) {
  tft.setTextSize(sz);
  tft.setTextColor(fg, bg);
  tft.drawString(s, (SCR_W - tft.textWidth(s)) / 2, y);
}
void btn(const char *s, int bx, int by, int bw, int bh, int fg, int bg, uint8_t sz) {
  tft.fillRoundRect(bx, by, bw, bh, 5, bg);
  tft.drawRoundRect(bx, by, bw, bh, 5, fg);  // border
  tft.setTextSize(sz);
  tft.setTextColor(fg, bg);
  int tx = bx + (bw - tft.textWidth(s)) / 2;
  int ty = by + (bh - sz * 8) / 2;
  tft.drawString(s, tx, ty > by ? ty : by + 2);
}
bool hit(int x, int y, int bx, int by, int bw, int bh) {
  return (x >= bx && x <= bx + bw && y >= by && y <= by + bh);
}
void drawDivider(int y) {
  tft.fillRect(0, y, SCR_W, 2, C_DIVIDER);
}
void drawHeader(const char *s, const char *right) {
  tft.fillScreen(C_BG);
  tft.fillRect(0, 0, SCR_W, HDR_H, C_HEADER);
  tft.setTextSize(1);
  tft.setTextColor(C_WHITE, C_HEADER);
  if (s) tft.drawString(s, 8, 7);
  if (right) {
    tft.setTextColor(C_ACCENT, C_HEADER);
    tft.drawString(right, SCR_W - 8 - strlen(right) * 6, 7);
  }
  drawDivider(HDR_H);
}

// prototypes
void drawScreen(); void handleTouch(); bool canInit();
int  findOrCreate(uint32_t id); void addToScan(uint32_t id, byte len, byte *data);
void sortByChanges();

// =============================================================
// SETUP
// =============================================================
void setup() {
  Serial.begin(115200);
  tft.begin(); tft.setRotation(1); tft.fillScreen(C_BG);
  tft.setTouch(tcal);
  canOk = canInit();
  drawScreen();
}

// =============================================================
// LOOP
// =============================================================
void loop() {
  yield();
  unsigned long now = millis();

  // CAN — SPI drain только при сканировании/мониторинге
  if (canOk && (mode == MODE_SCANNING || mode == MODE_MONITOR)) {
    int drainN = 0;
    while (CAN.checkReceive() == CAN_MSGAVAIL && drainN < 16) {
      drainN++;
      unsigned long rxId; byte len; byte buf[8];
      CAN.readMsgBufID(&rxId, &len, buf);
      canPush(rxId, len, buf);
    }
  }
  // В остальных режимах не трогаем MCP2515 вообще

  // Фаза 2: обработка FIFO (только при сканировании/мониторинге)
  if (mode == MODE_SCANNING || mode == MODE_MONITOR) {
    uint32_t qId; byte qLen; byte qData[8];
    int popN = 0;
    while (canPop(&qId, &qLen, qData) && popN < 16) {
      popN++;
      if (mode == MODE_SCANNING) { totalPkts++; addToScan(qId, qLen, qData); }
      else if (mode == MODE_MONITOR) {
        if (qId == monitorId && qLen >= 1) {
          totalPkts++;
          int v = qData[0];
          brightnessVal = (v > 100) ? map(v, 0, 255, 0, 100) : v;
          redrawNeeded = true;
        } else {
          totalPkts++;
        }
      }
    }
  }

  // Scanning
  if (mode == MODE_SCANNING) {
    if (now - lastTimerMs >= 250) {
      lastTimerMs = now;
      unsigned long remain = (scanStartMs + scanDurationMs > now)
        ? (scanStartMs + scanDurationMs - now) / 1000 : 0;
      int pct = ((now - scanStartMs) * 300) / scanDurationMs;
      if (pct > 300) pct = 300;
      tft.fillRect(10, 108, pct, 20, C_ACCENT);
      char tmp[24];
      sprintf(tmp, "Listening...  %2lus", (unsigned long)remain);
      tft.setTextSize(1); tft.setTextColor(C_WHITE, C_BG);
      tft.fillRect(10, 50, 300, 10, C_BG);
      tft.drawString(tmp, 10, 52);
    }
    // Pulse dot every ~500ms
    if (now - lastPulseMs >= 500) {
      lastPulseMs = now;
      pulseState = !pulseState;
      tft.fillCircle(18, 76, 4, pulseState ? C_GREEN : C_HEADER);
    }
    if (now - scanStartMs >= scanDurationMs) {
      canHead = 0; canTail = 0;
      mode = MODE_LIST; sortByChanges(); listPage = 0;
      redrawNeeded = true;  // отрисовка через основной dispatch — чистый стек
      delay(20);
      return;  // перезапуск loop() со свежим стеком
    }
  }

  if (redrawNeeded) { redrawNeeded = false; drawScreen(); }
  handleTouch();
  
  // Принудительный yield каждые 5ms для watchdog
  if (now - lastYieldMs >= 5) {
    lastYieldMs = now;
    delay(1);
  }
}

// =============================================================
// IDLE
// =============================================================
void drawIdle() {
  tft.fillScreen(C_BG);
  tft.fillRect(0, 0, SCR_W, 32, C_HEADER);
  tft.setTextSize(2);
  tft.setTextColor(C_ACCENT, C_HEADER);
  centerText("CAN HUNTER", 8, C_ACCENT, C_HEADER, 2);
  drawDivider(32);

  // Status with dot
  tft.fillCircle(18, 48, 5, canOk ? C_GREEN : C_RED);
  tft.setTextSize(1);
  tft.setTextColor(canOk ? C_GREEN : C_RED, C_BG);
  tft.drawString(canOk ? "MCP2515: OK  (500K)" : "MCP2515: NOT CONNECTED", 30, 44);

  // Instruction box
  int iy = 66;
  tft.drawRoundRect(8, iy, 304, 76, 4, C_LIST_BG);
  tft.setTextSize(1);
  tft.setTextColor(C_WHITE, C_BG);
  tft.drawString("1  Turn ignition ON", 20, iy + 10);
  tft.drawString("2  Press START SCAN", 20, iy + 26);
  tft.drawString("3  Rotate dimmer wheel", 20, iy + 42);
  tft.drawString("4  Tap ID from the list", 20, iy + 58);

  btn("START SCAN", 60, 180, 200, 42, TFT_BLACK, C_GREEN, 2);
}

// =============================================================
// SCANNING
// =============================================================
void drawScanning() {
  drawHeader("SCANNING...", NULL);

  // Status line
  tft.setTextSize(1);
  tft.setTextColor(C_WHITE, C_BG);
  char tmp[30];
  sprintf(tmp, "Listening...  %2lus", (unsigned long)(scanDurationMs / 1000));
  tft.drawString(tmp, 10, 52);

  // Pulse dot
  tft.fillCircle(18, 76, 4, C_GREEN);

  // Hint
  tft.setTextColor(C_GREY, C_BG);
  tft.drawString("Rotate dimmer wheel NOW", 30, 72);

  // Progress bar
  tft.fillRect(10, 108, 300, 20, C_LIST_BG);
  tft.drawRect(10, 108, 300, 20, C_WHITE);
  tft.fillRect(10, 108, 0, 20, C_ACCENT);

  btn("STOP", 100, 160, 120, 32, TFT_BLACK, C_RED, 2);
}

// =============================================================
// LIST
// =============================================================
void drawList() {
  int pages = sortedCount > 0 ? (sortedCount + PER_PAGE - 1) / PER_PAGE : 1;
  char pageStr[10];
  sprintf(pageStr, "%d/%d", listPage + 1, pages);
  drawHeader("FOUND IDs (by changes)", pageStr);

  int start = listPage * PER_PAGE;
  int itemsOnPage = sortedCount - start;
  if (itemsOnPage > PER_PAGE) itemsOnPage = PER_PAGE;

  if (sortedCount == 0) {
    tft.setTextSize(1);
    tft.setTextColor(C_GREY, C_BG);
    centerText("No CAN messages received.", 90, C_GREY, C_BG, 1);
    centerText("Check connection and try again.", 110, C_GREY, C_BG, 1);
  } else {
    int ry = HDR_H + 10;
    for (int i = 0; i < itemsOnPage; i++) {
      int idx = start + i;
      int si = sortedIdx[idx];
      ScanEntry *e = &scanBuf[si];

      // Row bg
      int rowH = 30;
      uint16_t rowBg = (i % 2 == 0) ? C_BG : C_LIST_BG;
      tft.fillRoundRect(6, ry, 308, rowH, 4, rowBg);
      tft.drawRoundRect(6, ry, 308, rowH, 4, C_DIVIDER);

      // ID (large)
      tft.setTextSize(2);
      tft.setTextColor(C_ACCENT, rowBg);
      char idStr[12];
      sprintf(idStr, "%03lX", e->id);
      tft.drawString(idStr, 14, ry + 6);

      // Stats (buf ДОЛЖЕН быть большим — count/changes уходят в тысячи)
      tft.setTextSize(1);
      char statStr[40];
      sprintf(statStr, "rx:%lu  chg:%lu", (unsigned long)e->count, (unsigned long)e->changes);
      tft.setTextColor(C_GREY, rowBg);
      tft.drawString(statStr, 110, ry + 5);

      // Change bar
      int barW = e->changes;
      if (barW > 200) barW = 200;
      if (barW > 0) {
        tft.fillRoundRect(110, ry + 18, barW / 2, 5, 2, C_WARM);
      }

      ry += rowH + 6;
    }
  }

  // Bottom bar
  drawDivider(210);
  
  if (listPage > 0) btn("<", 12, 214, 40, 22, TFT_BLACK, C_BLUE, 1);
  if (listPage + 1 < pages) btn(">", 58, 214, 40, 22, TFT_BLACK, C_BLUE, 1);
  btn("SCAN AGAIN", 200, 214, 110, 22, TFT_BLACK, C_GREEN, 1);
}

// =============================================================
// MONITOR
// =============================================================
void drawMonitor() {
  char tmp[40];
  sprintf(tmp, "MONITOR: 0x%03lX", monitorId);
  drawHeader(tmp, NULL);

  btn("BACK", 248, 3, 66, 18, TFT_BLACK, C_RED, 1);

  // Big round indicator
  int bx = 30, by = 32, bw = 260, bh = 154;
  tft.fillRoundRect(bx, by, bw, bh, 10, C_LIST_BG);
  tft.drawRoundRect(bx, by, bw, bh, 10, C_DIVIDER);

  // Fill from bottom
  int innerX = bx + 6, innerW = bw - 12;
  int barH = map(brightnessVal, 0, 100, 2, bh - 12);
  int barY = by + bh - 8 - barH;
  tft.fillRect(innerX, barY, innerW, barH, C_ACCENT);

  // Large percentage
  tft.setTextSize(4);
  tft.setTextColor(C_WHITE, C_LIST_BG);
  sprintf(tmp, "%3d%%", brightnessVal);
  centerText(tmp, (by + bh - 4 * 8) / 2, C_WHITE, C_LIST_BG, 4);

  // Info line
  tft.setTextSize(1);
  tft.setTextColor(C_GREY, C_LIST_BG);
  sprintf(tmp, "ID:0x%03lX  val:%d", monitorId, brightnessVal);
  centerText(tmp, by + bh - 14, C_GREY, C_LIST_BG, 1);

  // Bottom hint
  drawDivider(190);
  tft.setTextColor(C_GREY, C_BG);
  tft.drawString("Rotate wheel to see changes", 10, 200);
}

// =============================================================
// DISPATCH
// =============================================================
void drawScreen() {
  switch (mode) {
    case MODE_IDLE:     drawIdle();     break;
    case MODE_SCANNING: drawScanning(); break;
    case MODE_LIST:     drawList();     break;
    case MODE_MONITOR:  drawMonitor();  break;
  }
}

// =============================================================
// TOUCH
// =============================================================
void handleTouch() {
  static uint32_t lastT = 0;
  uint16_t x, y;
  if (!tft.getTouch(&x, &y)) return;
  if (millis() - lastT < TOUCH_D) return;
  lastT = millis();

  if (mode == MODE_SCANNING && hit(x, y, 100, 160, 120, 32)) {
    canHead = 0; canTail = 0;  // очищаем FIFO
    mode = MODE_LIST; sortByChanges(); listPage = 0; drawScreen();
    return;
  }

  if (mode == MODE_IDLE && hit(x, y, 60, 180, 200, 42)) {
    scanCount = 0; totalPkts = 0; scanDurationMs = 30000;
    for (int i = 0; i < MAX_SCAN_IDS; i++) scanBuf[i].active = false;
    scanStartMs = millis(); lastTimerMs = 0; lastPulseMs = 0;
    mode = MODE_SCANNING; drawScreen();
    return;
  }

  if (mode == MODE_LIST) {
    int start = listPage * PER_PAGE;
    int itemsOnPage = sortedCount - start;
    if (itemsOnPage > PER_PAGE) itemsOnPage = PER_PAGE;
    int ry = HDR_H + 10;
    for (int i = 0; i < itemsOnPage; i++) {
      if (hit(x, y, 6, ry, 308, 30)) {
        int idx = start + i;
        if (idx < sortedCount) {
          int si = sortedIdx[idx];
          monitorId = scanBuf[si].id;
          brightnessVal = 0;
          if (scanBuf[si].prevLen >= 1) {
            int v = scanBuf[si].prev[0];
            brightnessVal = (v > 100) ? map(v, 0, 255, 0, 100) : v;

          }
          mode = MODE_MONITOR; drawScreen();
        }
        return;
      }
      ry += 36;
    }

    int pages = sortedCount > 0 ? (sortedCount + PER_PAGE - 1) / PER_PAGE : 1;
    if (listPage > 0 && hit(x, y, 12, 214, 40, 22)) { listPage--; drawScreen(); return; }
    if (listPage + 1 < pages && hit(x, y, 58, 214, 40, 22)) { listPage++; drawScreen(); return; }
    if (hit(x, y, 200, 214, 110, 22)) { mode = MODE_IDLE; drawScreen(); return; }
    return;
  }

  if (mode == MODE_MONITOR && hit(x, y, 248, 3, 66, 18)) {
    mode = MODE_LIST; drawScreen();
  }
}

// =============================================================
// CAN INIT
// =============================================================
bool canInit() {
  SPI.begin(18, 19, 23, PIN_CAN_CS); delay(200);
  byte st = CAN.begin(CAN_SPEED, MCP_8MHz);
  if (st != CAN_OK) { delay(100); st = CAN.begin(CAN_SPEED, MCP_16MHz); }
  if (st == CAN_OK) { CAN.setMode(MODE_NORMAL); return true; }
  return false;
}

// =============================================================
// SCAN DATA
// =============================================================
int findOrCreate(uint32_t id) {
  for (int i = 0; i < MAX_SCAN_IDS; i++)
    if (scanBuf[i].active && scanBuf[i].id == id) return i;
  for (int i = 0; i < MAX_SCAN_IDS; i++)
    if (!scanBuf[i].active) {
      scanBuf[i].active = true; scanBuf[i].id = id;
      scanBuf[i].count = 0; scanBuf[i].changes = 0;
      scanBuf[i].prevLen = 0; memset(scanBuf[i].prev, 0, 8);
      return i;
    }
  return -1;
}

void addToScan(uint32_t id, byte len, byte *data) {
  int idx = findOrCreate(id); if (idx < 0) return;
  ScanEntry *e = &scanBuf[idx]; e->count++;
  bool changed = (e->prevLen != len);
  if (!changed) for (int i = 0; i < len; i++) if (e->prev[i] != data[i]) { changed = true; break; }
  if (changed) {
    e->changes++;
    int cl = (len < 8) ? len : 8;
    memcpy(e->prev, data, cl); e->prevLen = len;
  }
}

void sortByChanges() {
  sortedCount = 0;
  for (int i = 0; i < MAX_SCAN_IDS; i++)
    if (scanBuf[i].active) sortedIdx[sortedCount++] = i;
  for (int i = 0; i < sortedCount - 1; i++)
    for (int j = 0; j < sortedCount - 1 - i; j++) {
      bool sw = false;
      if (scanBuf[sortedIdx[j]].changes < scanBuf[sortedIdx[j+1]].changes) sw = true;
      else if (scanBuf[sortedIdx[j]].changes == scanBuf[sortedIdx[j+1]].changes
               && scanBuf[sortedIdx[j]].id > scanBuf[sortedIdx[j+1]].id) sw = true;
      if (sw) { int t = sortedIdx[j]; sortedIdx[j] = sortedIdx[j+1]; sortedIdx[j+1] = t; }
    }
}
