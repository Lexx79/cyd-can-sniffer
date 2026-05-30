import sys

src = r'D:\Gemini\cyd_can_sniffer\cyd_can_multitool\cyd_can_multitool.ino'

with open(src, 'r', encoding='utf-8') as f:
    content = f.read()

# Find and fix the handleAutoFilterTouch function
# The function uses wrong footerHit(0, tx) calls - needs footerHit("btn1","btn2","btn3",tx,ty)

old_func = '''void handleAutoFilterTouch(int tx, int ty) {
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
}'''

new_func = '''void handleAutoFilterTouch(int tx, int ty) {
  if (autoLearning) {
    // Learning phase: drawFooter(NULL, "CANCEL", NULL) -> btn2 = CANCEL
    int f = footerHit(NULL, "CANCEL", NULL, tx, ty);
    if (f == 2) {
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
    // Post-learning: drawFooter("SEARCH", "MENU", NULL) -> btn1=SEARCH btn2=MENU
    int f = footerHit("SEARCH", "MENU", NULL, tx, ty);
    if (f == 1) {
      // SEARCH
      autoSearching = true;
      autoEvtCnt = 0;
      redrawNeeded = true;
    } else if (f == 2) {
      mode = MODE_MONITOR_RAW;
      redrawNeeded = true;
    }
    return;
  }
  
  // Searching phase: drawFooter(NULL, "CLEAR", "MENU") -> btn2=CLEAR btn3=MENU
  int f = footerHit(NULL, "CLEAR", "MENU", tx, ty);
  if (f == 2) {
    // CLEAR
    autoEvtCnt = 0;
    redrawNeeded = true;
  } else if (f == 3) {
    // MENU - exit
    autoSearching = false;
    autoLearning = false;
    autoIdCnt = 0;
    autoEvtCnt = 0;
    mode = MODE_MONITOR_RAW;
    redrawNeeded = true;
  }
}'''

if old_func in content:
    content = content.replace(old_func, new_func)
    with open(src, 'w', encoding='utf-8') as f:
        f.write(content)
    print('OK - handleAutoFilterTouch fixed')
else:
    print('ERROR: exact text not found, trying partial...')
    # Try to find and replace just the footerHit lines
    remaining = content
    count = 0
    # Replace each wrong footerHit
    replacements = [
        ('if (footerHit(0, tx))', '// placeholder'),
    ]
    for old, new in replacements:
        if old in remaining:
            remaining = remaining.replace(old, new)
            count += 1
    print(f'Tried replacement: {count} changes')
