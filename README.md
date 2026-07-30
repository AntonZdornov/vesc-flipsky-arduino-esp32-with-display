# VESC Dashboard (ESP32-S3-Touch-LCD-1.69)

Дашборд для контроллера **Flipsky VESC**: ESP32 читает телеметрию по UART (библиотека `VescUart`) и рисует на встроенном LCD скорость (спидометр со стрелкой), заряд/напряжение батареи, температуру MOSFET, одометр (trip/total) и накопленную стоимость электроэнергии. UI собран на **LVGL v8.3.9**.

Экран используется **вертикально (портрет, 240×280)**.

## Экраны (листаются свайпом)

Четыре вкладки `lv_tabview` с горизонтальным свайпом; кнопки таба спрятаны, текущая вкладка видна по точкам внизу. При старте активен спидометр.

| Позиция | Вкладка | Содержимое |
|---|---|---|
| крайняя левая | **MOTOR CURRENT** | ток мотора: кнопки `−`/`+` (шаг 1 А, удержание крутит быстро, 0…60 А) и кнопка **UPDATE**, которая отправляет значение в VESC |
| левее центра | **SPEED LIMIT** | кнопка-тумблер ограничения **25 км/ч**; при каждом включении питания **ВЫКЛ** (в NVS не сохраняется) |
| центр (по умолчанию) | **Спидометр** | шкала Ø190 со стрелкой и дугой, крупная цифра, ниже строка «заряд % / напряжение / температура», под ней бейджи активных режимов (`LIMIT 25`, `I 12 A`) |
| правее центра | **Отчёт** | вертикально: `TRIP (SINCE BOOT)`, `TOTAL`, `COST, ILS` |

### Как работает вкладка тока (setCurrent)

`−`/`+` крутят только то, что показано на экране; в VESC ничего не уходит, пока не нажата **UPDATE**. После нажатия `loop()` повторяет `UART.setCurrent(значение)` в каждом цикле опроса (иначе команда затухнет по внутреннему таймауту VESC ~1 с), а под шкалой появляется бейдж `I N A`. Подпись `applied: N A` на вкладке всегда показывает, что реально отправляется.

⚠️ Это **прямая команда тока мотору** — после UPDATE с ненулевым значением мотор крутится, пока не выставишь `0` и не нажмёшь UPDATE снова. Потолок `CURRENT_MAX_A` (60 А) в [main/ui.cpp](main/ui.cpp) — сверь с motor current max в настройках VESC. Ограничение 25 км/ч приоритетнее: пока оно давит обороты, `setCurrent` не отправляется.

Свайпы работают через реальный тач **CST816T** ([main/touch.cpp](main/touch.cpp)), подключённый к LVGL как pointer-устройство.

### Как работает ограничение 25 км/ч

Пока тумблер включён и **показанная** скорость выше 25 км/ч, loop() шлёт `UART.setRPM(ui_kmh_to_erpm(25))` — VESC держит обороты, соответствующие 25 км/ч. Как только скорость ниже порога, команды не отправляются, и по внутреннему таймауту VESC (~1 с) управление возвращается ручке газа.

⚠️ Пока лимит давит, `setRPM` переводит контроллер в режим управления оборотами, то есть **перебивает ручку газа**. Порог считается по сглаженной скорости со спидометра, а eRPM — функцией `ui_kmh_to_erpm()` с теми же `POLE_PAIRS`/колесом, что и на дисплее (в [main/utils.cpp](main/utils.cpp) есть другая `kmh_to_erpm()` с иными константами — для лимита она не используется). Перед ездой проверь значение на стенде.

## Потоки: почему UI не подвисает

- **Задача `lvgl`** (`Lvgl_Start_Task()`, core 0, приоритет 2) крутит `lv_timer_handler()` каждые 5–20 мс: рендер, анимация стрелки, инерция свайпов.
- **`loop()`** (core 1, приоритет 1) только опрашивает VESC раз в 200 мс и больше не вызывает `delay(200)` — раньше именно он и морозил UI, так как `lv_timer_handler()` дёргался пять раз в секунду.
- LVGL не потокобезопасен, поэтому все `ui_set_*` из `loop()` обёрнуты в `Lvgl_Lock()/Lvgl_Unlock()` (рекурсивный мьютекс FreeRTOS).
- `full_refresh` выключен: перерисовываются только изменившиеся области, полный кадр 134 КБ по SPI не гоняется на каждый тик.

## Железо

Плата: **Waveshare ESP32-S3-Touch-LCD-1.69** (SKU 27350) — ESP32-S3R8 (240 МГц, 8 МБ PSRAM, 16 МБ flash), LCD **ST7789V2 240×280**, тач CST816T, IMU QMI8658C, RTC PCF85063, зарядка Li-ion.

Ориентация задаётся `LCD_PORTRAIT` в [main/display.h](main/display.h):

| `LCD_PORTRAIT` | Разрешение | MADCTL | Смещения |
|---|---|---|---|
| `1` (текущее) | 240×280, вертикально | `0x00` | `Offset_X=0`, `Offset_Y=20` |
| `0` | 280×240, ландшафт | `0x60` | `Offset_X=20`, `Offset_Y=0` |

Смещения нужны потому, что панель 240×280 живёт внутри памяти контроллера ST7789 240×320 и видимая область начинается со строки 20. Задаются они в логических координатах текущей ориентации, поэтому `LCD_SetCursor()` пишет их напрямую: X → CASET, Y → RASET.

⚠️ Вёрстка в [main/ui.cpp](main/ui.cpp) сделана **под портрет** (спидометр 190 px не влезает в трёхколоночную схему при ширине 240). Переключение `LCD_PORTRAIT` в `0` даст корректную геометрию экрана, но UI придётся переверстать.

### Распиновка

| Сигнал | GPIO | Где задаётся |
|---|---|---|
| LCD_DC | 4 | [main/display.h](main/display.h) |
| LCD_CS | 5 | [main/display.h](main/display.h) |
| LCD_SCL (SCLK) | 6 | [main/display.h](main/display.h) |
| LCD_MOSI (SDA) | 7 | [main/display.h](main/display.h) |
| LCD_RST | 8 | [main/display.h](main/display.h) |
| LCD_BL (подсветка, LEDC) | 15 | [main/display.h](main/display.h) |
| VESC UART RX | 3 | [main/main.ino](main/main.ino) |
| VESC UART TX | 2 | [main/main.ino](main/main.ino) |
| Тач CST816T: SCL / SDA | 10 / 11 | [main/touch.h](main/touch.h) |
| Тач CST816T: RST / INT | 13 / 14 | [main/touch.h](main/touch.h) |
| Кнопка (BOOT) | 0 | [main/main.ino](main/main.ino) |

MISO у LCD не разведён (`-1`, только запись). SPI-частота — **40 МГц**: GPIO6/GPIO7 не IOMUX-пины FSPI на S3, сигнал идёт через GPIO-матрицу, и 80 МГц работают нестабильно.

Свободные пады гребёнки: `GPIO2`, `GPIO3`, `GPIO17`, `GPIO18`, плюс `SCL=GPIO10 / SDA=GPIO11`, `TX=GPIO43 / RX=GPIO44`, питание `5V / 3V3 / GND`. VESC подключён к `GPIO3` (RX) и `GPIO2` (TX) — крест-накрест с UART-разъёмом VESC (VESC TX → GPIO3, VESC RX → GPIO2, GND общий).

Занято на плате (не использовать): 4/5/6/7/8/15 — LCD, 10/11 — I2C (тач+IMU+RTC), 13/14 — тач RST/INT, 1 — BAT_ADC, 19/20 — USB, 38/39 — INT сенсоров, 40/41 — SYS_OUT/SYS_EN (цепь питания от аккумулятора), 42 — баззер.

Подробная таблица железа: [waveshareteam/ESP32-S3-Touch-LCD-1.69 → HARDWARE_REFERENCE.md](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-1.69/blob/main/HARDWARE_REFERENCE.md).

### Не реализовано на этой плате

- **IMU / RTC / баззер / замер напряжения аккумулятора (GPIO1)** не используются.
- **Жесты CST816T** (двойной тап, свайпы самим чипом) не читаются — берём только координаты, жесты считает LVGL.
- **Питание от встроенного аккумулятора**: чтобы плата не выключалась после отпускания кнопки PWR, прошивка должна удерживать `SYS_EN` (GPIO41) в HIGH. Сейчас этого нет — питание ожидается внешнее (USB / 5V с VESC).
- **Адресного RGB-светодиода на плате нет.** [main/led.cpp](main/led.cpp) остался под внешнюю WS2812 (пин переведён с GPIO8, который здесь занят LCD_RST, на свободный GPIO17) и по-прежнему не вызывается из `setup()`.
- **BLE/Wi-Fi** ([main/ble_service.cpp](main/ble_service.cpp), [main/wifi_service.cpp](main/wifi_service.cpp)) — скаффолдинг, из `setup()` не вызывается.

## Сборка и прошивка

Собирается из **Arduino IDE** (или `arduino-cli`), скетч — [main/main.ino](main/main.ino).

1. Плата: **ESP32S3 Dev Module** (пакет `esp32` by Espressif, core **3.x** — используется `ledcAttach()`).
2. Настройки платы: Flash 16MB, PSRAM **OPI PSRAM**, Partition Scheme — любая с достаточным app-разделом, USB CDC On Boot — по вкусу (влияет на логи).
3. Библиотеки: `VescUart`, `lvgl` **8.3.9**, `NimBLEDevice`, `Adafruit_NeoPixel`.
4. [configs/lv_conf.h](configs/lv_conf.h) должен лежать на include-пути LVGL (в Arduino IDE — рядом с папкой `lvgl` в корне `libraries/`).

Тестов и линтеров в проекте нет.

## Настройка под свой транспорт

В [main/ui.cpp](main/ui.cpp): `POLE_PAIRS`, `WHEEL_DIAMETER_M`, `SPEED_MAX_KMH`, `SPEED_ALERT_KMH`, `METER_SIZE`, `CURRENT_STEP_A`, `CURRENT_MAX_A`.
В [main/main.ino](main/main.ino): `ELECTRICITY_RATE_ILS_PER_KWH` (тариф за кВт·ч), `TARGET_KMH` (порог лимита), `VESC_POLL_INTERVAL_MS`.
Логи включаются `DEBUG_MODE` в [main/logger.h](main/logger.h).

## PC-симулятор UI

Локальная сборка LVGL-дашборда под macOS через SDL2 — позволяет смотреть и править UI без прошивки ESP32.

### Установка зависимостей

```bash
brew install cmake sdl2 pkg-config
```

LVGL v8.3.9 и `lv_drivers` (release/v8.3) подтягиваются через CMake `FetchContent` автоматически — ставить вручную не нужно.

### Сборка

```bash
cd simulator
cmake -S . -B build      # первый раз скачает LVGL и lv_drivers (~30 сек)
cmake --build build -j   # инкрементальная сборка
```

### Запуск

```bash
./build/vesc_ui_sim
```

Откроется окно **240×280** с дашбордом. Вкладки листаются перетаскиванием мышью (мышь подставляется вместо тача). В цикле подаются фейковые данные (скорость гуляет 0–9000 eRPM, батарея медленно разряжается, температура растёт, одометр накручивается) — это позволяет проверить раскраску, переходы и форматирование.

### Структура

- [simulator/main.c](simulator/main.c) — точка входа, инициализация LVGL+SDL и фейковый драйвер VESC.
- [simulator/CMakeLists.txt](simulator/CMakeLists.txt) — сборка через FetchContent.
- [simulator/lv_drv_conf.h](simulator/lv_drv_conf.h) — настройки SDL-драйвера (`SDL_ZOOM`, разрешение — должно совпадать с `LCD_WIDTH`/`LCD_HEIGHT` из [main/display.h](main/display.h)).
- [simulator/arduino_shim.cpp](simulator/arduino_shim.cpp) — портативные копии `calcBatteryPercent`, `erpm_to_kmh`, `kmh_to_erpm` без зависимости от Arduino.
- [main/ui.h](main/ui.h) и [main/ui.cpp](main/ui.cpp) — общий UI-код, используется и прошивкой, и симулятором.

### Увеличить окно

Поменять `SDL_ZOOM` в [simulator/lv_drv_conf.h](simulator/lv_drv_conf.h):
- `1` → 240×280 (по умолчанию, 1:1 с экраном устройства)
- `3` → 720×840
- `4` → 960×1120

После изменения — пересобрать: `cmake --build build -j`.

## License
MIT License.
