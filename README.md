# ELM327 CAN to BLE Bridge

This project uses an ELM327 Wi-Fi adapter to read CAN bus data, process incoming messages, and broadcast them via BLE (Bluetooth Low Energy) to other devices.

## Features
- Connects to a vehicle's CAN bus through an ELM327 Wi-Fi adapter.
- Reads and parses CAN bus messages.
- Processes and filters data as needed.
- Transmits processed data over BLE to compatible devices (e.g., smartphones, tablets, or custom BLE clients).

## Use Cases
- Real-time vehicle diagnostics over BLE.
- Forwarding CAN bus data to custom BLE applications.
- Building mobile apps that receive live car data wirelessly.

## Use Cases
- Real-time vehicle diagnostics over BLE.
- Forwarding CAN bus data to custom BLE applications.
- Building mobile apps that receive live car data wirelessly.
- Retrieving hybrid battery charge level (SOC) from a Toyota Corolla Hybrid.
- Displaying SOC on a screen as a circular progress indicator with percentage using the LVGL graphics library.

## Requirements
- ELM327 Wi-Fi adapter.
- ESP32/ESP32-C3 or C6 (or compatible board with BLE support).
- BLE-enabled client device (smartphone or custom hardware).

## Future Improvements
- Support for additional OBD-II protocols.
- Customizable CAN filters.
- Bi-directional communication (BLE to CAN).



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

Откроется окно 320×172 с дашбордом. В цикле подаются фейковые данные (скорость гуляет 0–9000 eRPM, батарея медленно разряжается, температура растёт, одометр накручивается) — это позволяет проверить раскраску, переходы и форматирование.

### Структура

- [simulator/main.c](simulator/main.c) — точка входа, инициализация LVGL+SDL и фейковый драйвер VESC.
- [simulator/CMakeLists.txt](simulator/CMakeLists.txt) — сборка через FetchContent.
- [simulator/lv_drv_conf.h](simulator/lv_drv_conf.h) — настройки SDL-драйвера (`SDL_ZOOM`, разрешение).
- [simulator/arduino_shim.cpp](simulator/arduino_shim.cpp) — портативные копии `calcBatteryPercent`, `erpm_to_kmh`, `kmh_to_erpm` без зависимости от Arduino.
- [main/ui.h](main/ui.h) и [main/ui.cpp](main/ui.cpp) — общий UI-код, используется и прошивкой, и симулятором.

### Увеличить окно

Поменять `SDL_ZOOM` в [simulator/lv_drv_conf.h](simulator/lv_drv_conf.h):
- `1` → 320×172 (по умолчанию, 1:1 с экраном устройства)
- `3` → 960×516
- `4` → 1280×688

После изменения — пересобрать: `cmake --build build -j`.

## License
MIT License.
