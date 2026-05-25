/**
 * ESP32-2432S028 (CYD) — CAN ID Hunter v1.0
 *
 * Подключение MCP2515 (требуется пайка):
 *   SPI:     ножки ESP32 (SCK=18, MOSI=23, MISO=19)
 *   5V/GND:  площадки S3/S1 сзади платы
 *   CS:      разъём P3, пин 3 (GPIO22) — без пайки!
 *   CAN_H/L: OBD2 пины 6/14
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

// =============================================================
// ЦВЕТА UI
// =============================================================
#define C_BG         TFT_BLACK
#define C_HEADER     0x1A3C
#define C_DIVIDER    0x2945
#define C_LIST_BG    0x0841
#define C_ACCENT     TFT_CYAN
#define C_WARM       TFT_YELLOW
#define C_GREEN      TFT_GREEN
#define C_RED        TFT_RED
#define C_BLUE       0x29FF
#define C_TEXT       TFT_WHITE
#define C_DIM        0xAD55

// =============================================================
// РЕЖИМЫ
// =============================================================
enum AppMode { MODE_IDLE, MODE_SCANNING, MODE_LIST, MODE_MONITOR };
AppMode mode = MODE_IDLE;

// =============================================================
// ДАННЫЕ СКАНИРОВАНИЯ
// =============================================================
#define MAX_IDS 200
struct CanIdInfo {
  uint32_t id;
  uint32_t count;
  uint32_t changes;
  byte lastData[8];
  bool valid;
};
CanIdInfo ids[MAX_IDS];
int idCount = 0;

unsigned long scanStartMs = 0;
unsigned long scanDurationMs = 30000;
uint32_t totalPkts = 0;
unsigned long lastTimerMs = 0;

uint32_t monitorId = 0;
int brightnessVal = 0;

#define PER_PAGE 5
int listPage = 0;

bool canOk = false;

// =============================================================
// SETUP
// =============================================================
void setup() {
  Serial.begin(115200);
  tft.begin(); tft.setRotation(1); tft.fillScreen(C_BG);
  tft.setTouch(tcal);

  // CAN не инициализируем — будет попытка в canInit()
  drawIdle();
  Serial.println("CYD CAN ID Hunter v1.0");
  Serial.println("Waiting... START SCAN to begin");
}

// =============================================================
// CAN INIT
// =============================================================
bool canInit() {
  pinMode(PIN_CAN_CS, OUTPUT); digitalWrite(PIN_CAN_CS, HIGH);
  SPI.begin(18, 19, 23, PIN_CAN_CS); delay(200);
  byte st = CAN.begin(CAN_SPEED, MCP_8MHz);
  if (st != CAN_OK) { delay(100); st = CAN.begin(CAN_SPEED, MCP_16MHz); }
  if (st == CAN_OK) { CAN.setMode(MODE_NORMAL); return true; }
  return false;
}

// =============================================================
// ОБРАБОТКА ДАННЫХ CAN
// =============================================================
void processCanPacket(unsigned long rxId, byte len, byte* buf) {
  totalPkts++;
  // ищем ID в нашем массиве
  int idx = -1;
  for (int i = 0; i < idCount; i++) {
    if (ids[i].id == rxId) { idx = i; break; }
  }
  if (idx == -1) {
    if (idCount >= MAX_IDS) return;
    idx = idCount++;
    ids[idx].id = rxId;
    ids[idx].count = 0;
    ids[idx].changes = 0;
    memset(ids[idx].lastData, 0, 8);
    ids[idx].valid = true;
  }
  ids[idx].count++;
  // определяем изменения — сравниваем с последними данными
  if (ids[idx].count > 1) {
    for (byte i = 0; i < len && i < 8; i++) {
      if (buf[i] != ids[idx].lastData[i]) { ids[idx].changes++; break; }
    }
  }
  memcpy(ids[idx].lastData, buf, minLen(len, 8));
}

// =============================================================
// ВСПОМОГАТЕЛЬНЫЕ
// =============================================================
int minLen(int a, int b) { return (a < b) ? a : b; }

void drawDivider(int y, uint16_t color) {
  tft.fillRect(0, y, 320, 2, color);
}

void centerText(const char* s, int y, int font, uint16_t color, uint16_t bg) {
  tft.setTextColor(color, bg);
  tft.drawString(s, (320 - tft.textWidth(s)) / 2, y, font);
}

void btn(int x, int y, int w, int h, uint16_t bg, const char* label, int font) {
  tft.fillRoundRect(x, y, w, h, 6, bg);
  tft.drawRoundRect(x, y, w, h, 6, TFT_WHITE);
  tft.setTextColor(TFT_BLACK, bg);
  tft.drawString(label, x + (w - tft.textWidth(label)) / 2, y + (h - 8) / 2, font);
}

// =============================================================
// IDLE
// =============================================================
void drawIdle() {
  tft.fillScreen(C_BG);
  tft.fillRect(0, 0, 320, 28, C_HEADER);
  drawDivider(28, C_DIVIDER);
  centerText("CAN ID HUNTER", 5, 2, TFT_WHITE, C_HEADER);

  // статус MCP2515
  char st[40];
  sprintf(st, canOk ? "MCP2515:  OK" : "MCP2515:  WAIT");
  tft.setTextColor(canOk ? C_GREEN : C_RED, C_BG);
  tft.drawString(st, 10, 34, 1);

  // рамка с инструкцией
  tft.drawRoundRect(10, 55, 300, 115, 8, C_DIVIDER);

  int yy = 62;
  tft.setTextColor(C_ACCENT, C_BG);
  tft.drawString("How to find your ID:", 15, yy, 1); yy += 14;
  tft.setTextColor(TFT_WHITE, C_BG);
  tft.drawString("1. Turn ignition ON", 15, yy, 1); yy += 13;
  tft.drawString("2. Press START SCAN below", 15, yy, 1); yy += 13;
  tft.drawString("3. Rotate dimmer wheel for 30s", 15, yy, 1); yy += 13;
  tft.drawString("4. Tap the top ID in LIST", 15, yy, 1); yy += 13;
  tft.drawString("5. Check brightness bar in MONITOR", 15, yy, 1); yy += 13;

  btn(75, 180, 170, 42, C_GREEN, "START SCAN", 2);
}

// =============================================================
// SCANNING
// =============================================================
void drawScanning() {
  tft.fillScreen(C_BG);
  tft.fillRect(0, 0, 320, 28, C_HEADER);
  drawDivider(28, C_DIVIDER);

  // пульсирующая зелёная точка
  tft.fillCircle(10, 14, 4, (millis() / 500) % 2 ? C_GREEN : C_BG);
  centerText("SCANNING CAN BUS...", 5, 2, TFT_WHITE, C_HEADER);

  btn(260, 195, 55, 33, C_RED, "STOP", 1);
}

void updateScanning() {
  unsigned long elapsed = millis() - scanStartMs;
  int remaining = (elapsed > scanDurationMs) ? 0 : (int)((scanDurationMs - elapsed) / 1000);

  // прогресс-бар
  int barW = 280;
  int barX = 20;
  int barY = 40;
  int barH = 12;
  int fillW = (elapsed >= scanDurationMs) ? barW : (int)((long)elapsed * barW / scanDurationMs);
  tft.fillRect(barX, barY, barW, barH, C_LIST_BG);
  tft.fillRect(barX, barY, fillW, barH, C_ACCENT);
  tft.drawRect(barX, barY, barW, barH, C_DIVIDER);

  // пакеты и таймер на одной строке
  char line[60];
  sprintf(line, "Pkts:%lu  IDs:%d  Time:%ds", totalPkts, idCount, remaining);
  centerText(line, 60, 1, TFT_WHITE, C_BG);

  // подсказка
  tft.setTextColor(C_DIM, C_BG);
  tft.drawString("Rotate dimmer wheel now!", 40, 90, 1);
}

// =============================================================
// LIST
// =============================================================
void drawList() {
  tft.fillScreen(C_BG);
  tft.fillRect(0, 0, 320, 28, C_HEADER);
  drawDivider(28, C_DIVIDER);
  centerText("CAN IDs by changes", 5, 2, TFT_WHITE, C_HEADER);

  // сортировка по changes
  for (int i = 0; i < idCount - 1; i++) {
    for (int j = i + 1; j < idCount; j++) {
      if (ids[j].changes > ids[i].changes && ids[j].valid) {
        CanIdInfo tmp = ids[i]; ids[i] = ids[j]; ids[j] = tmp;
      }
    }
  }

  int totalPages = (idCount + PER_PAGE - 1) / PER_PAGE;
  if (totalPages < 1) totalPages = 1;
  if (listPage >= totalPages) listPage = totalPages - 1;

  int startIdx = listPage * PER_PAGE;
  int itemsOnPage = (startIdx + PER_PAGE > idCount) ? (idCount - startIdx) : PER_PAGE;

  for (int i = 0; i < itemsOnPage; i++) {
    int idx = startIdx + i;
    int rowY = 36 + i * 34;

    // чередование фона
    tft.fillRect(0, rowY, 320, 33, (i % 2 == 0) ? C_BG : C_LIST_BG);

    char idStr[20];
    sprintf(idStr, "0x%03lX", ids[idx].id);
    tft.setTextColor(C_ACCENT, (i % 2 == 0) ? C_BG : C_LIST_BG);
    tft.drawString(idStr, 8, rowY + 3, 2);

    // жёлтая полоска changes
    char chStr[20];
    sprintf(chStr, "ch:%lu", ids[idx].changes);
    tft.setTextColor(TFT_WHITE, (i % 2 == 0) ? C_BG : C_LIST_BG);
    tft.drawString(chStr, 110, rowY + 3, 1);

    char cntStr[20];
    sprintf(cntStr, "cnt:%lu", ids[idx].count);
    tft.setTextColor(C_DIM, (i % 2 == 0) ? C_BG : C_LIST_BG);
    tft.drawString(cntStr, 110, rowY + 16, 1);

    // рамка с жёлтым акцентом
    tft.drawRect(0, rowY + 31, 320, 1, C_WARM);
  }

  // пустые рамки не рисуем

  // пагинация
  if (listPage > 0) btn(10, 200, 50, 30, C_BLUE, "<", 2);
  if (listPage + 1 < totalPages) btn(260, 200, 50, 30, C_BLUE, ">", 2);

  char pg[20];
  sprintf(pg, "%d/%d", listPage + 1, totalPages);
  centerText(pg, 207, 1, TFT_WHITE, C_BG);

  btn(75, 200, 70, 30, C_BLUE, "SCAN AGAIN", 1);
}

// =============================================================
// MONITOR
// =============================================================
void drawMonitor() {
  tft.fillScreen(C_BG);
  tft.fillRect(0, 0, 320, 28, C_HEADER);
  drawDivider(28, C_DIVIDER);

  char hdr[40];
  sprintf(hdr, "0x%03lX  val:%d", monitorId, brightnessVal);
  centerText(hdr, 5, 2, TFT_WHITE, C_HEADER);

  // большой индикатор
  int indX = 40, indY = 50, indW = 240, indH = 120;
  tft.drawRoundRect(indX - 2, indY - 2, indW + 4, indH + 4, 10, C_DIVIDER);

  int fillH = (brightnessVal * indH) / 100;
  if (fillH > 0) {
    int fillColor = (brightnessVal < 25) ? C_GREEN : (brightnessVal < 75) ? TFT_YELLOW : C_RED;
    tft.fillRect(indX, indY + indH - fillH, indW, fillH, fillColor);
  }

  // процент внутри индикатора
  char pct[10];
  sprintf(pct, "%d%%", brightnessVal);
  tft.setTextColor(TFT_WHITE, C_BG);
  tft.setTextSize(2);
  tft.drawString(pct, indX + indW / 2 - tft.textWidth(pct) / 2, indY + indH / 2 - 10, 2);
  tft.setTextSize(1);

  btn(100, 195, 120, 35, C_BLUE, "BACK", 2);
}

void updateMonitor(unsigned long rxId, byte len, byte* buf) {
  if (rxId != monitorId) return;
  int v = (len > 0) ? (int)buf[0] : 0;
  brightnessVal = (v > 100) ? map(v, 0, 255, 0, 100) : v;
  drawMonitor();
}

// =============================================================
// ОБРАБОТКА ТАЧСКРИНА
// =============================================================
uint16_t tx, ty;
bool touched;

bool isBtn(int x, int y, int w, int h) {
  return (tx >= x && tx <= x + w && ty >= y && ty <= y + h);
}

void handleTouch() {
  touched = tft.getTouch(&tx, &ty);
  if (!touched) return;

  if (mode == MODE_IDLE && isBtn(75, 180, 170, 42)) {
    // START SCAN
    scanStartMs = millis();
    totalPkts = 0; idCount = 0;
    for (int i = 0; i < MAX_IDS; i++) ids[i].valid = false;
    lastTimerMs = 0;

    if (!canOk) canOk = canInit();
    mode = MODE_SCANNING;
    drawScanning();
    return;
  }

  if (mode == MODE_SCANNING && isBtn(260, 195, 55, 33)) {
    // STOP
    mode = MODE_LIST; listPage = 0;
    drawList();
    return;
  }

  if (mode == MODE_LIST) {
    // тап по ID
    for (int i = 0; i < 5; i++) {
      int rowY = 36 + i * 34;
      if (tx >= 0 && tx <= 320 && ty >= rowY && ty <= rowY + 32) {
        int startIdx = listPage * PER_PAGE;
        if (startIdx + i < idCount && ids[startIdx + i].valid) {
          monitorId = ids[startIdx + i].id;
          brightnessVal = 0;
          mode = MODE_MONITOR;
          drawMonitor();
          return;
        }
      }
    }
    // <
    if (listPage > 0 && isBtn(10, 200, 50, 30)) { listPage--; drawList(); return; }
    // >
    int totalPages = (idCount + PER_PAGE - 1) / PER_PAGE;
    if (totalPages < 1) totalPages = 1;
    if (listPage + 1 < totalPages && isBtn(260, 200, 50, 30)) { listPage++; drawList(); return; }
    // SCAN AGAIN
    if (isBtn(75, 200, 70, 30)) { mode = MODE_IDLE; drawIdle(); return; }
  }

  if (mode == MODE_MONITOR && isBtn(100, 195, 120, 35)) {
    // BACK
    mode = MODE_LIST; drawList();
    return;
  }
}

// =============================================================
// LOOP
// =============================================================
void loop() {
  handleTouch();

  if (mode == MODE_SCANNING && canOk) {
    // приём CAN-пакетов (drain со счётчиком, MCP2515 всего 3 RX буфера)
    int canLimit = 100;
    while (canLimit-- > 0 && CAN.checkReceive() == CAN_MSGAVAIL) {
      unsigned long rxId; byte len = 0; byte buf[8];
      CAN.readMsgBufID(&rxId, &len, buf);
      processCanPacket(rxId, len, buf);
    }

    // обновление таймера/прогресса
    unsigned long now = millis();
    if (now - lastTimerMs > 250) {
      lastTimerMs = now;
      drawScanning(); // точка пульсирует
      updateScanning();

      // проверка окончания таймаута
      if (now - scanStartMs >= scanDurationMs) {
        mode = MODE_LIST; listPage = 0;
        drawList();
      }
    }
  }

  if (mode == MODE_MONITOR && canOk) {
    int canLimit = 100;
    while (canLimit-- > 0 && CAN.checkReceive() == CAN_MSGAVAIL) {
      unsigned long rxId; byte len = 0; byte buf[8];
      CAN.readMsgBufID(&rxId, &len, buf);
      updateMonitor(rxId, len, buf);
    }
  }

  yield();
}
