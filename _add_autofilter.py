import sys, os

src = r'D:\Gemini\cyd_can_sniffer\cyd_can_multitool\cyd_can_multitool.ino'

with open(src, 'r', encoding='utf-8') as f:
    lines = f.readlines()

# ============================================================
# 1. Add MODE_AUTO_FILTER to enum
# ============================================================
enum_start = None
for i, line in enumerate(lines):
    if 'MODE_DIMMER,' in line.rstrip():
        enum_start = i
        break

if enum_start:
    # Insert before MODE_DIMMER (or after - doesn't matter)
    lines.insert(enum_start, '  MODE_AUTO_FILTER,\n')
    print(f'1. MODE_AUTO_FILTER added to enum at line {enum_start+1}')
else:
    print('ERROR: enum not found')
    sys.exit(1)

# ============================================================
# 2. Add data structures after global vars section
# ============================================================
# Find where to insert - after dimmerValue/etc declarations
anchor = None
for i, line in enumerate(lines):
    if 'int prevBrightness = -1;' in line.rstrip() and anchor is None:
        anchor = i
        break

if anchor:
    struct_block = '''\
// -- AUTO FILTER globals --
#define MAX_AUTO_IDS 80
#define MAX_AUTO_EVENTS 40

struct AutoFilterEntry {
  uint32_t id;
  uint8_t templ[8];
  uint8_t len;
  bool stable[8];
  bool used;
};

struct AutoFilterEvent {
  uint32_t id;
  uint8_t bIdx;
  uint8_t oldV;
  uint8_t newV;
};

AutoFilterEntry autoIds[MAX_AUTO_IDS];
AutoFilterEvent autoEvts[MAX_AUTO_EVENTS];
int autoIdCnt = 0, autoEvtCnt = 0;
bool autoLearning = false, autoSearching = false;
unsigned long autoLearnStartMs = 0;
#define AUTO_LEARN_MS 30000UL

'''
    lines.insert(anchor + 1, struct_block)
    print(f'2. AutoFilter structs inserted after line {anchor+1}')
else:
    print('ERROR: anchor not found')
    sys.exit(1)

# ============================================================
# 3. Add capture logic in processCanData (after dimmer capture)
# ============================================================
dimmer_capture = None
for i, line in enumerate(lines):
    if 'if (msg.id == 0x294 && msg.len >= 2) {' in line:
        dimmer_capture = i
        break

if dimmer_capture:
    # Find the closing brace of the dimmer block
    # The pattern is: if (msg.id == 0x294 ...) { ... } then sensor configurable slots
    # Insert after the closing } of dimmer block
    auto_capture = '''\
    // AUTO FILTER capture (always active during learning/search)
    if (autoLearning || autoSearching) {
      int ai = -1;
      for (int x = 0; x < autoIdCnt; x++) { if (autoIds[x].id == msg.id) { ai = x; break; } }
      if (ai < 0) {
        // New ID during learning
        if (autoIdCnt < MAX_AUTO_IDS) {
          ai = autoIdCnt++;
          autoIds[ai].id = msg.id;
          autoIds[ai].len = (msg.len < 8) ? msg.len : 8;
          for (int b = 0; b < autoIds[ai].len; b++) {
            autoIds[ai].templ[b] = msg.data[b];
            autoIds[ai].stable[b] = true;
          }
          autoIds[ai].used = true;
        }
      } else {
        // Existing ID - check for changes
        uint8_t l = (msg.len < 8) ? msg.len : 8;
        if (autoLearning) {
          // Learning: mark bytes that changed as unstable
          for (int b = 0; b < l; b++) {
            if (b < autoIds[ai].len && msg.data[b] != autoIds[ai].templ[b]) {
              autoIds[ai].stable[b] = false;
            }
          }
        } else if (autoSearching) {
          // Searching: detect stable-bytes that changed
          for (int b = 0; b < l; b++) {
            if (b < autoIds[ai].len && autoIds[ai].stable[b] && msg.data[b] != autoIds[ai].templ[b]) {
              // Log event
              if (autoEvtCnt < MAX_AUTO_EVENTS) {
                autoEvts[autoEvtCnt].id = msg.id;
                autoEvts[autoEvtCnt].bIdx = b;
                autoEvts[autoEvtCnt].oldV = autoIds[ai].templ[b];
                autoEvts[autoEvtCnt].newV = msg.data[b];
                autoEvtCnt++;
              }
              autoIds[ai].templ[b] = msg.data[b]; // update template
              valueUpdateNeeded = true;
            }
          }
        }
      }
    }

'''
    # Insert after the sensor loop close. Let's find the line with "}" that closes dimmer block
    # Look for: dimmerValue = ..., then next line closes with }
    lines.insert(dimmer_capture + 4, auto_capture)
    print(f'3. Auto filter capture inserted after line {dimmer_capture+1}')
else:
    print('ERROR: dimmer capture not found')
    sys.exit(1)

# ============================================================
# 4. Add drawAutoFilter / handleAutoFilterTouch / updateAutoFilterValue
# ============================================================
# Find insertion point - before the main loop's MODE switches
# Actually, let's find where handleDimmerTouch is and put auto filter functions after it
insert_after = None
for i, line in enumerate(lines):
    if 'void handleDimmerTouch(int tx, int ty)' in line:
        insert_after = i
        break

if insert_after:
    # Find the end of handleDimmerTouch function
    brace_count = 0
    end_func = insert_after
    for i in range(insert_after, len(lines)):
        line = lines[i].strip()
        if '{' in line:
            brace_count += line.count('{')
        if '}' in line:
            brace_count -= line.count('}')
            if brace_count == 0:
                end_func = i + 1  # line after closing brace
                break
    
    if end_func:
        auto_funcs = '''\n
// =============================================================
//  MODE: AUTO FILTER (learn stable bytes, then detect changes)
// =============================================================
void drawAutoFilter() {
  drawHeader("AUTO FILTER", NULL);
  tft.setFreeFont(&FreeSansBold9pt7b);
  tft.setTextSize(1);
  
  if (autoLearning) {
    // Phase 1: learning in progress
    unsigned long elapsed = millis() - autoLearnStartMs;
    int remain = (AUTO_LEARN_MS - elapsed) / 1000;
    if (remain < 0) remain = 0;
    
    char tmp[50];
    sprintf(tmp, "LEARNING... %ds", remain);
    tft.setTextColor(C_ACCENT, C_BG);
    tft.drawString(tmp, 10, 50);
    
    sprintf(tmp, "IDs captured: %d", autoIdCnt);
    tft.setTextColor(C_GREY, C_BG);
    tft.drawString(tmp, 10, 80);
    
    // Show progress bar
    int barW = 300, barH = 20, barX = 10, barY = 110;
    float pct = (float)elapsed / AUTO_LEARN_MS;
    if (pct > 1.0f) pct = 1.0f;
    tft.drawRect(barX, barY, barW, barH, C_DIVIDER);
    if (pct > 0) tft.fillRect(barX + 1, barY + 1, (int)((barW - 2) * pct), barH - 2, C_ACCENT);
    
    tft.drawString("Remain still, do nothing", 10, 145);
    tft.drawString("Wait for timer to finish", 10, 165);
    
    drawFooter(NULL, "CANCEL", NULL);
    
  } else if (!autoSearching) {
    // Phase 1 complete, waiting for SEARCH
    char tmp[50];
    sprintf(tmp, "LEARNED: %d IDs", autoIdCnt);
    tft.setTextColor(C_ACCENT, C_BG);
    tft.drawString(tmp, 10, 50);
    
    // Count total stable bytes
    int stableTotal = 0;
    for (int i = 0; i < autoIdCnt; i++) {
      for (int b = 0; b < autoIds[i].len; b++) {
        if (autoIds[i].stable[b]) stableTotal++;
      }
    }
    sprintf(tmp, "Stable bytes: %d", stableTotal);
    tft.setTextColor(C_GREY, C_BG);
    tft.drawString(tmp, 10, 80);
    
    sprintf(tmp, "Unstable bytes: ~%d", autoIdCnt * 8 - stableTotal);
    tft.drawString(tmp, 10, 100);
    
    tft.setTextColor(C_WHITE, C_BG);
    tft.drawString("Press SEARCH to start monitoring", 10, 140);
    tft.setTextColor(C_GREY, C_BG);
    tft.drawString("Then perform actions (turn signal,", 10, 160);
    tft.drawString("open door, press buttons, etc.)", 10, 177);
    
    drawFooter("SEARCH", "MENU", NULL);
    
  } else {
    // Phase 2: actively searching
    tft.setTextColor(C_ACCENT, C_BG);
    tft.drawString("SEARCHING... act now!", 10, 50);
    
    char tmp[50];
    sprintf(tmp, "Watching %d IDs", autoIdCnt);
    tft.setTextColor(C_GREY, C_BG);
    tft.drawString(tmp, 10, 75);
    
    // Show last events
    int y = 100;
    for (int i = max(0, autoEvtCnt - 10); i < autoEvtCnt; i++) {
      sprintf(tmp, "0x%03lX b[%d]: 0x%02X->0x%02X", 
              autoEvts[i].id, autoEvts[i].bIdx, autoEvts[i].oldV, autoEvts[i].newV);
      tft.setTextColor(C_GREEN, C_BG);
      tft.drawString(tmp, 10, y);
      y += 16;
      if (y > 210) break;
    }
    
    drawFooter(NULL, "CLEAR", "MENU");
  }
}

void updateAutoFilterValue() {
  // Only redraw event list if searching and we have new events
  if (!autoSearching || autoEvtCnt == 0) return;
  // Force redraw is enough since drawAutoFilter shows the latest events
}

void handleAutoFilterTouch(int tx, int ty) {
  if (autoLearning) {
    // CANCEL button
    if (footerHit(0, tx)) {
      autoLearning = false;
      autoSearching = false;
      autoIdCnt = 0;
      autoEvtCnt = 0;
      mode = MODE_MONITOR_RAW;
      redrawNeeded = true;
    }
    return;
  }
  
  if (!autoSearching) {
    // Phase 1 done: SEARCH or MENU
    if (footerHit(0, tx)) {
      // SEARCH
      autoSearching = true;
      autoEvtCnt = 0;
      redrawNeeded = true;
    } else if (footerHit(2, tx) || footerHit(1, tx)) {
      mode = MODE_MONITOR_RAW;
      redrawNeeded = true;
    }
    return;
  }
  
  // Searching phase
  if (footerHit(1, tx)) {
    // CLEAR
    autoEvtCnt = 0;
    redrawNeeded = true;
  } else if (footerHit(2, tx)) {
    // MENU
    autoSearching = false;
    autoLearning = false;
    autoIdCnt = 0;
    autoEvtCnt = 0;
    mode = MODE_MONITOR_RAW;
    redrawNeeded = true;
  }
}

'''
        lines.insert(end_func, auto_funcs)
        print(f'4. Auto filter functions inserted after handleDimmerTouch')
else:
    print('ERROR: handleDimmerTouch not found')
    sys.exit(1)

# ============================================================
# 5. Add MODE_AUTO_FILTER to main loop switch/cases
# ============================================================
# Find draw handler  
for i, line in enumerate(lines):
    if 'case MODE_DIMMER:        drawDimmer(); break;' in line:
        lines.insert(i, '    case MODE_AUTO_FILTER:  drawAutoFilter(); break;\n')
        print(f'5a. drawAutoFilter added at line {i+1}')
        break

# Find touch handler
for i, line in enumerate(lines):
    if 'case MODE_DIMMER:       handleDimmerTouch(tx, ty); break;' in line:
        lines.insert(i, '    case MODE_AUTO_FILTER:  handleAutoFilterTouch(tx, ty); break;\n')
        print(f'5b. handleAutoFilterTouch added at line {i+1}')
        break

# Find update handler
for i, line in enumerate(lines):
    if 'case MODE_DIMMER:        updateDimmerValue(); break;' in line:
        lines.insert(i, '      case MODE_AUTO_FILTER:  updateAutoFilterValue(); break;\n')
        print(f'5c. updateAutoFilterValue added at line {i+1}')
        break

# ============================================================
# 6. Add AUTO button to MONITOR footer
# ============================================================
# Find drawMonitorRaw() drawFooter call
for i, line in enumerate(lines):
    if 'drawFooter(NULL, \"MENU\", NULL);' in line:
        prev = lines[i-1].strip() if i > 0 else ''
        # Only if we're in monitor raw context
        if 'MONITOR' in prev or 'monitor' in lines[max(0,i-5)].lower():
            lines[i] = '  drawFooter("AUTO", "MENU", NULL);\n'
            print(f'6. MONITOR footer updated with AUTO button at line {i+1}')
            break

# ============================================================
# 7. Handle AUTO button touch in MONITOR touch handler
# ============================================================
for i, line in enumerate(lines):
    if 'void handleMonitorRawTouch(int tx, int ty)' in line:
        # Find the footer handling inside
        for j in range(i, min(i+30, len(lines))):
            if 'footerHit(0, tx)' in lines[j] or 'footerHit' in lines[j]:
                # This is handleMonitorRawTouch footer section - need to add AUTO
                pass
        break

# Actually MONITOR uses drawFooter(NULL, "MENU", NULL) so only footerHit(1,tx) for MENU
# Now with "AUTO", "MENU" - footerHit(0) = AUTO, footerHit(1) = MENU
# Let me find the MONITOR touch handler and update it
for i, line in enumerate(lines):
    if 'void handleMonitorRawTouch(int tx, int ty)' in line:
        for j in range(i, min(i+30, len(lines))):
            if 'footerHit(1, tx)' in lines[j]:
                # Replace the whole block that handles footer in monitor
                original = ''.join(lines[j:j+5])  # context for debug
                print(f'7. Found MONITOR touch footer at line {j+1}: {lines[j].strip()}')
                # Insert AUTO filter logic before the existing footer handling
                lines.insert(j, '''  // AUTO FILTER button
  if (footerHit(0, tx)) {
    // Start auto filter learning phase
    autoLearning = true;
    autoSearching = false;
    autoIdCnt = 0;
    autoEvtCnt = 0;
    autoLearnStartMs = millis();
    mode = MODE_AUTO_FILTER;
    redrawNeeded = true;
    return;
  }
  
''')
                print(f'7. AUTO button handler added before line {j+1}')
                break
        break

# ============================================================
# Write result
# ============================================================
out = ''.join(lines)
with open(src, 'w', encoding='utf-8') as f:
    f.write(out)

print(f'\nDone! {len(lines)} lines written')
