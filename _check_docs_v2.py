import os

fp = open('opisanie proekta.md', encoding='utf-8')
c = fp.read()
fp.close()
print('Total chars:', len(c))
checks = ['AUTO FILTER', '0x164', 'LIGHT_CONTROL', 'BLINKERS', '0x20=LEFT', '0x40=RIGHT', 'patch1', 'flicker']
for ch in checks:
    if ch in c:
        print('  ' + ch + ': YES')
    else:
        print('  ' + ch + ': MISSING')
