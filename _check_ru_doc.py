# -*- coding: utf-8 -*-
r = open(r'D:\Gemini\cyd_can_sniffer\описание проекта.md', encoding='utf-8').read()
i = r.find('Необходимые библиотеки Arduino')
if i > 0:
    print("Section FOUND at position", i)
    print(repr(r[i:i+250]))
elif 'Библиотеки' in r:
    i = r.find('Библиотеки')
    print("Partial match:", repr(r[max(0,i-50):i+200]))
else:
    print("Section NOT FOUND")
    if 'Апгрейды' in r:
        i = r.find('Апгрейды')
        print("Upgrades starts:", repr(r[max(0,i-50):i+200]))
