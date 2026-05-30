import sys, io
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8')

src = r'D:\Gemini\cyd_can_sniffer\cyd_can_multitool\cyd_can_multitool.ino'
with open(src, 'r', encoding='utf-8') as f:
    content = f.read()

changes = []

# ========== BUG 1: DIMMER — remove prevBrightness from capture ==========
old = 'if (msg.id == 0x294 && msg.len >= 2) { dimmerValue = msg.data[1]; if (dimmerValue != prevBrightness) { prevBrightness = dimmerValue; valueUpdateNeeded = true; } }'
new = 'if (msg.id == 0x294 && msg.len >= 2) { dimmerValue = msg.data[1]; if (dimmerValue != prevBrightness) { valueUpdateNeeded = true; } }'
if old in content:
    content = content.replace(old, new)
    changes.append('1. DIMMER: removed prevBrightness from capture')
else:
    changes.append('1. DIMMER: SKIP (already fixed or not found)')

# ========== BUG 2: drawSpeedo() fillRect width ==========
old = '  if (speedoMode == SPEEDO_MODE_SPEED) {\n    tft.fillRect(10, 70, 300, 60, C_BG);'
new = '  if (speedoMode == SPEEDO_MODE_SPEED) {\n    tft.fillRect(0, 70, SCR_W, 60, C_BG);'
if old in content:
    content = content.replace(old, new)
    changes.append('2. drawSpeedo SPD: fillRect 300->SCR_W')
else:
    changes.append('2. drawSpeedo SPD: SKIP')

old = '  } else if (speedoMode == SPEEDO_MODE_RPM) {\n    tft.fillRect(10, 70, 300, 60, C_BG);'
new = '  } else if (speedoMode == SPEEDO_MODE_RPM) {\n    tft.fillRect(0, 70, SCR_W, 60, C_BG);'
if old in content:
    content = content.replace(old, new)
    changes.append('2. drawSpeedo RPM: fillRect 300->SCR_W')
else:
    changes.append('2. drawSpeedo RPM: SKIP')

old = '  } else if (speedoMode == SPEEDO_MODE_BOTH) {\n    tft.fillRect(10, 40, 300, 170, C_BG);'
new = '  } else if (speedoMode == SPEEDO_MODE_BOTH) {\n    tft.fillRect(0, 40, SCR_W, 170, C_BG);'
if old in content:
    content = content.replace(old, new)
    changes.append('2. drawSpeedo BOTH: fillRect 300->SCR_W')
else:
    changes.append('2. drawSpeedo BOTH: SKIP (may already be SCR_W)')

# ========== BUG 3: Move AUTO FILTER capture OUT of sensor slot loop ==========
# Current: auto filter is INSIDE the `for (int si...)` and `if (sensorCanId[si] != 0 && msg.id == sensorCanId[si])`
# Fix: Move auto filter block to right after the sensor slot loop closes

# The sensor slot block ends with } // close sensorCanId check, then } // close for loop
# We need to find the exact text to replace
old_autofilter_trapped = '''    // Configurable sensor slots: check each against incoming messages
    for (int si = 0; si < SENSOR_SLOTS; si++) {
      if (sensorCanId[si] != 0 && msg.id == sensorCanId[si]) {
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

    int b = sensorDataByte[si];'''

new_autofilter_free = '''    // Configurable sensor slots: check each against incoming messages
    for (int si = 0; si < SENSOR_SLOTS; si++) {
      if (sensorCanId[si] != 0 && msg.id == sensorCanId[si]) {
    int b = sensorDataByte[si];'''

if old_autofilter_trapped in content:
    content = content.replace(old_autofilter_trapped, new_autofilter_free)
    changes.append('3. AUTO FILTER: freed from sensor slot loop')
else:
    changes.append('3. AUTO FILTER: FAILED (text mismatch)')

# Now add the auto filter block AFTER the sensor slots loop closes
# Find where the sensor processing ends (after the sensor slot check + update)
# Look for: } // end of if(sensorCanId) } // end of for(si) followed by blank line then // Capture for Monitor
old_after_sensors = '''      if (sensorValue[si] != prevSensorValue[si]) {
        prevSensorValue[si] = sensorValue[si];
        if (mode == MODE_SENSORS) valueUpdateNeeded = true;
      }
    }
    }

    // Capture for Monitor (mode-gated, uses monitorId)'''

new_after_sensors = '''      if (sensorValue[si] != prevSensorValue[si]) {
        prevSensorValue[si] = sensorValue[si];
        if (mode == MODE_SENSORS) valueUpdateNeeded = true;
      }
    }
    }

    // AUTO FILTER capture (always active, independent of sensor slots)
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
              if (autoEvtCnt < MAX_AUTO_EVENTS) {
                autoEvts[autoEvtCnt].id = msg.id;
                autoEvts[autoEvtCnt].bIdx = b;
                autoEvts[autoEvtCnt].oldV = autoIds[ai].templ[b];
                autoEvts[autoEvtCnt].newV = msg.data[b];
                autoEvtCnt++;
              }
              autoIds[ai].templ[b] = msg.data[b];
              valueUpdateNeeded = true;
            }
          }
        }
      }
    }

    // Capture for Monitor (mode-gated, uses monitorId)'''

if old_after_sensors in content:
    content = content.replace(old_after_sensors, new_after_sensors)
    changes.append('3. AUTO FILTER: insert free-standing block after sensor loop')
else:
    changes.append('3. AUTO FILTER: FAILED to find after-sensors anchor')

# ========== BUG 4: SCAN→MONITOR — auto-select first scan ID ==========
# When entering MONITOR from menu with monitorId=0, auto-show first scanned ID
# Fix: in drawMonitorRaw(), if monitorId == 0, try to use first scanBuf entry
old_monitor_draw = '''void drawMonitorRaw() {
  char tmp[48];
  sprintf(tmp, "MONITOR: 0x%03lX", monitorId);'''
new_monitor_draw = '''void drawMonitorRaw() {
  // Auto-select first scanned ID if monitorId is 0
  if (monitorId == 0 && scanCount > 0) {
    monitorId = scanBuf[0].id;
    monitorHasData = false;
    prevMonitorHasData = false;
  }
  char tmp[48];
  sprintf(tmp, "MONITOR: 0x%03lX", monitorId);'''
if old_monitor_draw in content:
    content = content.replace(old_monitor_draw, new_monitor_draw)
    changes.append('4. MONITOR: auto-select first scan ID')
else:
    changes.append('4. MONITOR: SKIP (text mismatch)')

# ========== BUG 5: MONITOR HINT when no data yet ==========
# Show hint "No data for this ID yet" instead of blank screen
old_monitor_hint = '''  // Hex bytes
  tft.setFreeFont(&FreeSansBold9pt7b);
  tft.setTextSize(1);
  tft.setTextColor(C_GREY, C_BG);
  if (monitorHasData) {'''
new_monitor_hint = '''  // Hex bytes
  tft.setFreeFont(&FreeSansBold9pt7b);
  tft.setTextSize(1);
  tft.setTextColor(C_GREY, C_BG);
  if (monitorHasData) {'''
# Actually this looks the same but there's no else clause. Let me check if there's a hint...

# Let me add: if !monitorHasData, show "Waiting for CAN data..."
old_monitor_after = '''  if (monitorHasData) {
    char hex[40]; hex[0] = 0;
    for (int i = 0; i < monitorLen; i++) {
      sprintf(hex + strlen(hex), "%02X ", monitorBytes[i]);
    }
    sprintf(tmp, "HEX: %s", hex);
    tft.drawString(tmp, 10, 50);
  }'''
# This exact text has indentation issues. Let me check by reading the actual file.

with open(src, 'r', encoding='utf-8') as f:
    lines = f.readlines()
# Find the exact text around line 704
found = False
for i,l in enumerate(lines):
    if 'if (monitorHasData)' in l and 'hex[40]' not in lines[i+1] if i+1 < len(lines) else '':
        # This is the first one
        for j in range(i, i+12):
            print(f'   {j+1}: {repr(lines[j])}'[-120:])
        found = True
        break

with open(src, 'w', encoding='utf-8') as f:
    f.write(content)

print('\n=== RESULTS ===')
for c in changes:
    print(c)
print('\nRemaining: auto filter timer expiry (in main loop section)')
