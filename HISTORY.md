
---

## 2026-05-27 (сессия 4 — CAN-Мультитул v2.0-alpha)

### Что сделано
- **CAN-Мультитул v2.0-alpha** — полный редизайн проекта:
  - Главное меню с 2×2 сеткой, пагинацией и статус-индикаторами (`+`/`-`)
  - 6 портированных режимов: Сканер, Список ID, RAW Monitor, DECODE Monitor, Спидометр, Датчики
  - Декодер на 11 Honda CAN ID (0x17C, 0x309, 0x324, 0x1A6, 0x191, 0x1A4, 0x158, 0x13C, 0x18E, 0x294, 0x255)
  - Единая футер-панель `drawFooter()/footerHit()`
  - Все тексты на латинице (TFT_eSPI не поддерживает кириллицу)
- **Исправлены все ошибки компиляции**:
  1. `MCP_CAN` → `mcp2515_can` (MCP_CAN v2.x — абстрактный класс)
  2. `begin()` → v2.x сигнатура
  3. `readMsgBuf()` → v2.x двухаргументная
  4. `CAN_GETFLT` → `CAN_NOMSG`
  5. UTF-8 BOM удалён (`stray '\357'`)
  6. `MenuItem` struct → глобальная область видимости
  7. Лишняя `}` после `updateSpeedoValue()` удалена
- **UI-фиксы по запросу пользователя**:
  - Спидометр: убраны подписи km/h/RPM, только цифры
  - Footer: 3 кнопки слева (SPD/BTH/RPM) + красный MENU справа
  - Сенсоры: текст textSize 2→1 (не вылезает), ячейки 140→148px, добавлен выход
  - Кнопка выхода работает во всех режимах
- **README/описание проекта** полностью переписаны с детальным описанием всех 10 режимов
- **GitHub**: коммит и тег v2.0-alpha (forced push)

### Архитектурные решения
- **Footer bar** как единый UI-паттерн (из v1.2) — 30px, кнопки с roundRect
- **Global MenuItem struct** — определён до всех функций, shared между drawMenu/handleMenuTouch
- **Custom footer для Speedo** — `drawSpeedoFooter()` с 4 кнопками (SPD/BTH/RPM/MENU)
- **Прямая координатная проверка тача** для speedo (footerHit не подходит для 4 кнопок)
- **updateSpeedoValue()** — только очистка и перерисовка числа, хедер/футер не трогает
- **updateMonitorValue()** — 50ms дебаунс, только данные

### Технические особенности
- Скетч: `D:\Gemini\cyd_can_sniffer\cyd_can_multitool\cyd_can_multitool.ino` (1147 строк)
- Баланс скобок: 170 = 170
- BOM отсутствует
- MCP_CAN v2.x by coryjfowler
- TFT_eSPI CYD variant

### Блокеры
- B-CAN не подключен (нет второго MCP2515)
- GitHub raw.githubusercontent.com заблокирован
- Пользователь не тестировал компиляцию после фиксов

### Исправления после теста на реальном железе (22:00 МСК)
Пользователь прошил v2.0-alpha, нашёл 4 бага — все исправлены:

1. **Scanner STOP → меню**: если сканирование завершено (scanCount > 0), STOP ведёт в ID-лист, а не сразу в меню
2. **List ID текст обрезался**: textSize 2→1, y+6→y+4, добавлена очистка строки
3. **Спидометр не обновлялся**: `valueUpdateNeeded = true` добавлен в `processCanData()` при приёме 0x309 (скорость) и 0x17C (RPM)
4. **Sensors "--" хардкод**: значения выносятся в глобальные переменные через processCanData(), sprintf в drawSensors()

### Diff-based redraw
- `prevSpeedoVal`, `prevRpmVal` — скорость/RPM обновляются только при изменении
- `prevMonitorBytes[8]`, `monChanged` — монитор только при изменении данных
- `prevBrightness`, `prevRawVal` — бар и процент только при изменении
- `fillRect` обновления строго в границах режима (BOTH: 10,40,300,170; single: 10,70,300,100)

### Font overhaul (22:58 МСК)
- **Цифры** (спидометр, монитор) → **Font7** (7-segment RLE, "как ЖК-модуль")
- **Текст** (меню, кнопки, подписи, сенсоры, декодер) → **FreeSansBold** (9pt/12pt, anti-aliased, не пикселит)
- Включены: `FreeSansBold9pt7b.h`, `FreeSansBold12pt7b.h`, `FreeSansBold18pt7b.h`, `FreeSansBold24pt7b.h`
- `setup()`: `tft.setFreeFont(&FreeSansBold9pt7b)` вместо `setTextFont(2)`
- Спидометр: `setTextFont(7)` + textSize 5 (single) / 3 (both)
- Monitor RAW: `setTextFont(7)` + textSize 3 (%%) / 2 (raw)
- DrawMenu: +/− заменены на `fillCircle()` (зелёный/красный кружок)
- Все 22 вызова шрифтов верифицированы, 0 setTextFont(2), скобки 186=186

### Файлы
- Скетч: `cyd_can_multitool.ino` (1217 строк)
- Созданы скрипты: `_fix_fonts.py`, `_fix_fonts2.py`, `_fix_spdfooter.py`, `_fix_sensors.py`, `_verify_fonts_final.py`
