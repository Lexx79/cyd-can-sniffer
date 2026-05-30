with open(r'D:\Gemini\cyd_can_sniffer\cyd_can_multitool\cyd_can_multitool.ino', 'r', encoding='utf-8') as f:
    content = f.read()

# Fix fillRect widths in updateSpeedoValue - use SCR_W for full clear
content = content.replace(
    '    tft.fillRect(10, 70, 300, 60, C_BG);\n    tft.setTextFont(7);\n    tft.setTextSize(2);\n    sprintf(tmp, "%4d", speedoValue);\n    int tw = tft.textWidth(tmp);\n    tft.setTextColor(C_ACCENT, C_BG);\n    tft.drawString(tmp, (SCR_W - tw) / 2, 80);\n\n  } else if (speedoMode == SPEEDO_MODE_RPM) {\n    if (rpmValue == lastDrawnRpm) return;\n    lastDrawnRpm = rpmValue;\n    tft.fillRect(10, 70, 300, 60, C_BG);',
    '    tft.fillRect(0, 70, SCR_W, 60, C_BG);\n    tft.setTextFont(7);\n    tft.setTextSize(2);\n    sprintf(tmp, "%4d", speedoValue);\n    int tw = tft.textWidth(tmp);\n    tft.setTextColor(C_ACCENT, C_BG);\n    tft.drawString(tmp, (SCR_W - tw) / 2, 80);\n\n  } else if (speedoMode == SPEEDO_MODE_RPM) {\n    if (rpmValue == lastDrawnRpm) return;\n    lastDrawnRpm = rpmValue;\n    tft.fillRect(0, 70, SCR_W, 60, C_BG);'
)

# Fix BOTH mode clears too
content = content.replace(
    '      tft.fillRect(10, 40, 300, 85, C_BG);\n      tft.setTextFont(7);\n      tft.setTextSize(1);\n      sprintf(tmp, "%4d", speedoValue);\n      int tw = tft.textWidth(tmp);\n      tft.setTextColor(C_ACCENT, C_BG);\n      tft.drawString(tmp, (SCR_W - tw) / 2, 50);\n    }\n\n    if (rpmChanged) {\n      lastDrawnRpm = rpmValue;\n      tft.fillRect(10, 125, 300, 85, C_BG);',
    '      tft.fillRect(0, 40, SCR_W, 85, C_BG);\n      tft.setTextFont(7);\n      tft.setTextSize(1);\n      sprintf(tmp, "%4d", speedoValue);\n      int tw = tft.textWidth(tmp);\n      tft.setTextColor(C_ACCENT, C_BG);\n      tft.drawString(tmp, (SCR_W - tw) / 2, 50);\n    }\n\n    if (rpmChanged) {\n      lastDrawnRpm = rpmValue;\n      tft.fillRect(0, 125, SCR_W, 85, C_BG);'
)

with open(r'D:\Gemini\cyd_can_sniffer\cyd_can_multitool\cyd_can_multitool.ino', 'w', encoding='utf-8') as f:
    f.write(content)

print('Done - fillRect widths updated to SCR_W')
