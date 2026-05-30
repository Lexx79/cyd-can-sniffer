
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

---

## 2026-05-28 (сессия 5 — Bugfix blitz + speed 0x158 + MONITOR cleanup)

### Что сделано
- **Font7 размеры уменьшены вдвое**: speedo SINGLE 4→2, BOTH 2→1, monitor raw 2→1
- **Sensor live updates fixed**: убран `mode == MODE_SENSORS` guard в processCanData() — захват CAN ID теперь не зависит от режима
- **Добавлены prev-trackers** для всех сенсоров + `updateSensorsValue()` с дифф-обновлением только значений
- **Sensor cell edit**: CAN ID строки удалены, высота ячейки 50px, 3×2 сетка — только Label + Value
- **Coolant fix**: восстановлен захват 0x324 (data[0]-40)
- **Picker tap fix**: убран `if (f == 0) return;` — теперь тапы по строкам работают
- **Speedo flicker fix**: updateSpeedoValue() больше не вызывает drawSpeedo() — инлайн обновление только цифр
- **0x309 CAR_SPEED parsing fixed**: по opendbc DBC — Intel LE start_bit=7, bytes 0-1-2
- **0x158 ENGINE_DATA = PRIMARY speed**: BYTE[4:5]=SPEEDOMETER(MPH×0.01) с конвертацией в км/ч, захват добавлен
- **0x309 убран из скорости**: оставлен только 0x158, 0x309 теперь только в SCAN/MONITOR для экспериментов
- **MONITOR cleanup**: удалён DECODE mode, удалена гистограмма и проценты — только RAW HEX байты во весь экран
- **drawList font fix**: добавлен `tft.setFreeFont(&FreeSansBold9pt7b)` в начало функции (Font7 из MONITOR не сбрасывался)
- **Service manual скачан целиком** (haccord.org): 65 страниц, 540 файлов в `D:\Gemini\Honda_Accord8_Service_Manual\`
- **Canny.ru данные добавлены** в `Honda_Toyota_CAN_ID_Map.xlsx`: 9 новых листов (~170 ID: BMW, MB, Ford, Mazda, Peugeot, Opel)

### Архитектурные решения
- Скорость: только 0x158 (ENGINE_DATA bytes 4-5 MPH→km/h). 0x309 выключен как ненадёжный источник
- MONITOR: только RAW. Никакой DECODE, гистограммы или процентов
- Sensor slots: слот 0 = 0x158 (Speed) вместо 0x309
- Сенсоры авто-назначаются: 0=Speed, 1=RPM, 2=Coolant (dataByte=-1) = используют глобальные декодированные значения

### Технические особенности
- Скетч: ~1400 строк
- Меню: 10 режимов с индикацией +/-
- Diff-based redraw: speedo, RPM, monitor, sensors
- Debug/fix скрипты: более 20 в `_fix_*.py`

### Планы (приостановлены)
- v2.0 считается финальной на данный момент. Релиз с текущими изменениями.
- При необходимости — экспериментальный режим с B-CAN (нужен второй MCP2515)

---

## 2026-05-29 (сессия 6 — DIMMER mode + 0x294 dimmer discovery + menu 2×3 + багфиксы)

### 0x294 Dimmer Discovery (17:29)
- BYTE[1] 0x294 SCM_FEEDBACK = подсветка приборки
- **0–15**: плавная регулировка яркости с фарами (0=тускло, 15=ярко)
- **0x60 (96)**: максимальная яркость (крутилка до упора, не сбрасывается при выкл. фар)
- **16 (0x10)**: авто-значение при выключенных фарах
- Добавлен в Excel и код декодера

### Speed 0x158 — KMH, не MPH (17:35)
- User test: показания в 1.5 раза выше (60 приборка → 90 на экране)
- **Вывод: 0x158 BYTE[4:5] = km/h×0.01, НЕ MPH**
- Убран `* 1.60934f` из захвата и декодера

### MONITOR font fix (17:38)
- HEX строка не влезала в 320px при FreeSansBold12pt7b
- Переключено на FreeSansBold9pt7b

### Font corruption fix (17:41)
- При входе в MONITOR и выходе обратно в меню — шрифты не сбрасывались
- **Причина**: drawHeader(), drawMenu(), drawScanner() не вызывали setFreeFont()
- **Фикс**: все draw* функции теперь явно ставят FreeSansBold9pt7b в начале

### Menu 2×3 + MODE_DIMMER (17:47)
- Меню переделано: 2 колонки × 3 ряда = 6 режимов на странице
- Кнопки 152×48px, лучше для тапа
- Добавлен MODE_DIMMER (#5, рабочий) — баровый режим с большим Font7 hex числом
- **Порядок**: [1]SCAN [2]MONITOR [3]SPEEDO [4]SENSORS [5]DIMMER [6-8]planned
- DIMMER: Font7 число + горизонтальный бар + подпись Bright: N
- Diff-based redraw через prevBrightness
- v2.0 тег пересоздан

### Технические особенности
- Скетч: ~1450 строк
- Меню: 8 пунктов на 2 страницах
- Режимов: 6 (SCAN, LIST, MONITOR, SPEEDO, SENSORS, DIMMER) + 1 внутренний (SENSOR_PICK)
- Рабочих: 5 (DIMMER добавлен)


---

## 2026-05-29 (сессия 6 — CAN-Мультитул v2.2: DIMMER + финальные фиксы)

### Что сделано
- **0x294 BYTE[1] = DIMMER подсветки приборки** (главное открытие сессии)
  - 0x00 = минимум (тускло)
  - 0x01..0x15 = плавная регулировка (включены фары)
  - 0x96 (150) = МАКСИМУМ (прыжок с 0x15)
  - Захват всегда активен через processCanData()
- **MODE_DIMMER** — новый режим CAN-Мультитула:
  - Font7 size 1 hex-цифра (крупно, по центру)
  - Горизонтальный bar (map 0..150, красный при 0x96)
  - Информационная строка: dim=0 mid=0x15 MAX=0x96
  - Diff-based redraw (prevBrightness)
- **Скорость исправлена**: 0x158 BYTE[4:5] = KMH×0.01 (НЕ MPH)
  - Убран *1.60934 — данные уже в км/ч
- **Меню 2×3**: 6 кнопок (152×48px) на страницу, DIMMER на позиции 5
- **char tmp[10] → char tmp[50]**: устранены все buffer overflow crash
- **MONITOR**: memcmp по всем 8 байтам (не только byte[0])
- **MONITOR IDX строка**: теперь тоже обновляется в updateMonitorRawValue()
- **Все draw* функции** форсируют шрифт (font corruption fix)
- **Релиз**: v2.0 tag удалён → v2.2 tag создан

### Релизная структура
- Рабочий код: D:\Gemini\cyd_can_sniffer\cyd_can_multitool\cyd_can_multitool.ino
- Релиз: D:\Gemini\cyd_can_sniffer\cyd_can_multitool_v2.2\cyd_can_multitool_v2.2.ino
- Оба внутри D:\Gemini\cyd_can_sniffer\


---

## 2026-05-30 (session 7 — Anti-flicker + финальные правки)

### Что сделано
- **DIMMER точный диапазон**: 0x00→0x15(smooth mid) 0x96=MAX (вместо ошибочного 0x60)
- **Speedo anti-flicker**: static lastDrawnSpeed/lastDrawnRpm предотвращают перерисовку нулевой скорости при изменении оборотов
- **BOTH mode независимая перерисовка**: скорость и RPM проверяются раздельно
- **SCR_W fill fix**: fillRect во всю ширину экрана — артефакты Font7 при смене 3/4 знаков устранены
- **Убрана надпись** "Rotate dimmer wheel NOW" из режима сканирования
- **Документация**: Excel, README, описание проекта, HISTORY — все обновлены
- **Релиз v2.2 обновлён**: скопирован в cyd_can_multitool_v2.2/

### Ключевые решения
- static переменные для last-drawn — простое и надёжное решение без глобальных флагов
- Каждый режим (SPD/RPM/BOTH) независимо проверяет свои static-значения
