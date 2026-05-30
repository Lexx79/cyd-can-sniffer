import sys, io
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8')

src = r'D:\Gemini\cyd_can_sniffer\cyd_can_multitool\cyd_can_multitool.ino'
with open(src, 'r', encoding='utf-8') as f:
    content = f.read()

changes = []

# ====== BUG 1: DIMMER not updating ======
# Capture sets prevBrightness BEFORE updateDimmerValue checks it
# Fix: remove prevBrightness = dimmerValue from capture
old_294 = 'if (msg.id == 0x294 && msg.len >= 2) { dimmerValue = msg.data[1]; if (dimmerValue != prevBrightness) { prevBrightness = dimmerValue; valueUpdateNeeded = true; } }'
new_294 = 'if (msg.id == 0x294 && msg.len >= 2) { dimmerValue = msg.data[1]; if (dimmerValue != prevBrightness) { valueUpdateNeeded = true; } }'
if old_294 in content:
    content = content.replace(old_294, new_294)
    changes.append('BUG 1: DIMMER - removed prevBrightness from capture')
else:
    changes.append('BUG 1: DIMMER - FAILED (text mismatch)')

# ====== BUG 2: drawSpeedo() fillRect width 300 vs SCR_W ======
# Fix drawSpeedo full-render function fillRects to use SCR_W
old_draw_speedo_spd = '''  if (speedoMode == SPEEDO_MODE_SPEED) {
    tft.fillRect(10, 70, 300, 60, C_BG);
    tft.setTextFont(7);
    tft.setTextSize(2);
    sprintf(tmp, "%4d", speedoValue);
    int tw = tft.textWidth(tmp);
    tft.setTextColor(C_ACCENT, C_BG);
    tft.drawString(tmp, (SCR_W - tw) / 2, 80);

  } else if (speedoMode == SPEEDO_MODE_RPM) {
    tft.fillRect(10, 70, 300, 60, C_BG);
    tft.setTextFont(7);
    tft.setTextSize(2);
    sprintf(tmp, "%4d", rpmValue);
    int tw = tft.textWidth(tmp);
    tft.setTextColor(C_GREEN, C_BG);
    tft.drawString(tmp, (SCR_W - tw) / 2, 80);

  } else if (speedoMode == SPEEDO_MODE_BOTH) {
    tft.fillRect(10, 40, 300, 170, C_BG);

    tft.setTextFont(7);
    tft.setTextSize(1);
    sprintf(tmp, "%4d", speedoValue);
    int tw = tft.textWidth(tmp);
    tft.setTextColor(C_ACCENT, C_BG);
    tft.drawString(tmp, (SCR_W - tw) / 2, 50);

    tft.setTextFont(7);
    tft.setTextSize(1);
    sprintf(tmp, "%4d", rpmValue);
    tw = tft.textWidth(tmp);
    tft.setTextColor(C_GREEN, C_BG);
    tft.drawString(tmp, (SCR_W - tw) / 2, 140);
  }'''

new_draw_speedo_spd = '''  if (speedoMode == SPEEDO_MODE_SPEED) {
    tft.fillRect(0, 70, SCR_W, 60, C_BG);
    tft.setTextFont(7);
    tft.setTextSize(2);
    sprintf(tmp, "%4d", speedoValue);
    int tw = tft.textWidth(tmp);
    tft.setTextColor(C_ACCENT, C_BG);
    tft.drawString(tmp, (SCR_W - tw) / 2, 80);

  } else if (speedoMode == SPEEDO_MODE_RPM) {
    tft.fillRect(0, 70, SCR_W, 60, C_BG);
    tft.setTextFont(7);
    tft.setTextSize(2);
    sprintf(tmp, "%4d", rpmValue);
    int tw = tft.textWidth(tmp);
    tft.setTextColor(C_GREEN, C_BG);
    tft.drawString(tmp, (SCR_W - tw) / 2, 80);

  } else if (speedoMode == SPEEDO_MODE_BOTH) {
    tft.fillRect(0, 40, SCR_W, 170, C_BG);

    tft.setTextFont(7);
    tft.setTextSize(1);
    sprintf(tmp, "%4d", speedoValue);
    int tw = tft.textWidth(tmp);
    tft.setTextColor(C_ACCENT, C_BG);
    tft.drawString(tmp, (SCR_W - tw) / 2, 50);

    tft.setTextFont(7);
    tft.setTextSize(1);
    sprintf(tmp, "%4d", rpmValue);
    tw = tft.textWidth(tmp);
    tft.setTextColor(C_GREEN, C_BG);
    tft.drawString(tmp, (SCR_W - tw) / 2, 140);
  }'''

if old_draw_speedo_spd in content:
    content = content.replace(old_draw_speedo_spd, new_draw_speedo_spd)
    changes.append('BUG 2: drawSpeedo() fillRect 300->SCR_W')
else:
    changes.append('BUG 2: drawSpeedo() - FAILED')

# ====== BUG 4: AUTO FILTER timer never updates ======
# Need to add periodic redraw during learning phase
# Find the main loop update section and add auto filter timer check
old_main_update = '''  if (valueUpdateNeeded) {
    valueUpdateNeeded = false;
    switch (mode) {'''
new_main_update = '''  // AUTO FILTER periodic redraw (timer countdown)
  if (autoLearning && (millis() - autoLearnStartMs >= AUTO_LEARN_MS)) {
    // Learning complete
    autoLearning = false;
    autoSearching = false;
    redrawNeeded = true;
  }
  // Force periodic redraw for auto learning timer display
  if (autoLearning && redrawAllowed()) {
    static unsigned long lastAutoDraw = 0;
    unsigned long now = millis();
    if (now - lastAutoDraw >= 500) {
      lastAutoDraw = now;
      redrawNeeded = true;
    }
  }
  
  if (valueUpdateNeeded) {
    valueUpdateNeeded = false;
    switch (mode) {'''

if old_main_update in content:
    content = content.replace(old_main_update, new_main_update)
    changes.append('BUG 4: AUTO FILTER timer period redraw + expiry')
else:
    changes.append('BUG 4: AUTO FILTER timer - FAILED')

with open(src, 'w', encoding='utf-8') as f:
    f.write(content)

for c in changes:
    print(c)
print(f'\nFile size: {len(content)} chars')
