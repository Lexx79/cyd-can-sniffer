import os
files = ['HISTORY.md', 'README.md', 'opisanije_proekta.md']
checks = {
    'AUTO FILTER': ['AUTO FILTER'],
    '0x164 LIGHT_CONTROL': ['0x164'],
    'BLINKERS': ['BLINKERS', '0x20', '0x40'],
    'flicker fix': ['flicker'],
    'patch1': ['patch1'],
}
# map to real filename
realnames = {'HISTORY.md', 'README.md', 'opisanije_proekta.md'}
for name in realnames:
    if os.path.exists('HISTORY.md'): pass
for f in files:
    real = 'opisanie proekta.md' if 'opisanije' in f else f
    if not os.path.exists(real):
        print(f'{real}: NOT FOUND')
        continue
    with open(real, encoding='utf-8') as fp:
        c = fp.read()
    ok = True
    for label, terms in checks.items():
        present = all(t in c for t in terms)
        if not present:
            ok = False
            missing = [t for t in terms if t not in c]
            print(f'{real}: MISSING {label}: {missing}')
    if ok:
        print(f'{real}: OK')
