# VESC Dashboard (ESP32-S3-Touch-LCD-1.69)

Dashboard for a **Flipsky VESC**: the ESP32 reads telemetry over UART (`VescUart`) and renders speed, wheel power, battery charge/voltage, MOSFET temperature, odometer and electricity cost with **LVGL v8.3.9**. It also commands the controller: a 25 km/h limit, direct motor current, and a code lock. Screen is used in **portrait, 240x280**.

## Hardware and wiring

Board: **Waveshare ESP32-S3-Touch-LCD-1.69** (SKU 27350) — ESP32-S3R8 (240 MHz, 8 MB PSRAM, 16 MB flash), **ST7789V2 240x280** LCD, CST816T touch, QMI8658C IMU, PCF85063 RTC, Li-ion charging.

| Signal | GPIO | Defined in |
|---|---|---|
| LCD_DC / CS / SCLK / MOSI / RST | 4 / 5 / 6 / 7 / 8 | [main/display.h](main/display.h) |
| LCD_BL (backlight, LEDC) | 15 | [main/display.h](main/display.h) |
| VESC UART RX / TX | 44 / 43 | [main/main.ino](main/main.ino) |
| Touch SCL / SDA | 10 / 11 | [main/touch.h](main/touch.h) |
| Touch RST / INT | 13 / 14 | [main/touch.h](main/touch.h) |
| Button (BOOT) | 0 | [main/main.ino](main/main.ino) |

- **VESC UART link:** crossed over — VESC TX → GPIO44, VESC RX → GPIO43, common GND. `HardwareSerial(1)` at 115200 baud, polled every `VESC_POLL_INTERVAL_MS` (200 ms).
- LCD MISO is not routed (`-1`, write only). SPI clock is **40 MHz** — GPIO6/7 are not FSPI IOMUX pins on the S3, so the signal goes through the GPIO matrix and 80 MHz is unstable.
- Touch: CST816T over I2C, coordinates only (LVGL derives gestures).
- Free pads: `GPIO2`, `GPIO3`, `GPIO17`, `GPIO18`, plus `5V / 3V3 / GND`.
- Taken (do not use): 4/5/6/7/8/15 LCD, 10/11 I2C, 13/14 touch, 1 BAT_ADC, 19/20 USB, 38/39 sensor INTs, 40/41 SYS_OUT/SYS_EN, 42 buzzer.
- Orientation via `LCD_PORTRAIT` in [main/display.h](main/display.h): `1` = 240x280 / MADCTL `0x00` / `Offset_X=0, Offset_Y=20`; `0` = 280x240 / `0x60` / `Offset_X=20, Offset_Y=0`. Offsets exist because the panel starts at row 20 of the ST7789's 240x320 memory. ⚠️ [main/ui.cpp](main/ui.cpp) is laid out for portrait only.
- Not implemented: IMU, RTC, buzzer, battery ADC, built-in battery operation (needs `SYS_EN`/GPIO41 held HIGH), BLE, Wi-Fi, RGB LED.

Full hardware table: [waveshareteam/ESP32-S3-Touch-LCD-1.69 → HARDWARE_REFERENCE.md](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-1.69/blob/main/HARDWARE_REFERENCE.md).

## Build and flash

Arduino IDE (or `arduino-cli`); sketch is [main/main.ino](main/main.ino). No tests or linters.

1. Board: **ESP32S3 Dev Module**, `esp32` core **3.x** (`ledcAttach()` is used).
2. Settings: Flash 16MB, PSRAM **OPI PSRAM**, any partition scheme with a large enough app partition.
3. Libraries: `VescUart` and `lvgl` **8.3.9**.
4. [configs/lv_conf.h](configs/lv_conf.h) must be on LVGL's include path (Arduino IDE: next to the `lvgl` folder in the root of `libraries/`).

## VESC control protocol

Both the 25 km/h limit and the lock patch the motor config with a raw `COMM_SET_MCCONF_TEMP` packet ([main/vesc_limit.cpp](main/vesc_limit.cpp)) rather than sending movement commands:

- **Limit:** `l_min_erpm`/`l_max_erpm` = ±`ui_kmh_to_erpm(25)`.
- **Lock:** `l_current_max_scale = 0`, plus `UART.setBrakeCurrent(LOCK_BRAKE_A)` (6 A) repeated every poll as a holding brake. `l_current_min_scale` stays 1.0 so the brake survives.

The packet rewrites all eight limit fields at once, so both modes are folded into one `VescLimits` struct and sent in a single transmission. `store = 0` (RAM only), so it is re-sent every `LIMIT_REFRESH_MS` (3 s) while any mode is active — this is what makes the state survive a VESC reboot.

⚠️ Turning the limit off sends ±`NO_LIMIT_ERPM` (100000), which **overwrites** the Max ERPM from VESC Tool until the controller reboots. Raise the constant in [main/main.ino](main/main.ino) if the real config is higher; verify on a bench before riding.

⚠️ Do not reimplement the limit with `UART.setRPM()` — it does not work: the VESC's ADC/PPM throttle app rewrites the setpoint at ~1 kHz and overwrites 5 Hz UART commands.

**Motor current (`setCurrent`):** `−`/`+` only move the on-screen value; **UPDATE** applies it, after which `loop()` repeats `UART.setCurrent(value)` every poll (a single send decays on the VESC's ~1 s timeout). Pressing again when applied == configured turns it off (red `OFF`). ⚠️ Direct current command — the motor spins until `OFF`. Cross-check `CURRENT_MAX_A` (60 A) against motor current max in VESC settings. Not sent while the lock is on.

**Lock:** code **1987** (`LOCK_CODE`), same code locks and unlocks. State is stored in NVS and pushed to the VESC on the first `loop()` iteration after boot. While active, tab-0 current and the BOOT-button `setDuty` are suppressed. ⚠️ Not an anti-theft device — the clamp lives in VESC RAM and dies with power, and the code is plaintext in firmware. The holding brake drains battery and heats the motor if the scooter is pushed.

## Screens (swipe to page)

`lv_tabview` with hidden tab buttons; position shown by the bottom dots. Speedometer active at boot.

| # | Tab | Contents |
|---|---|---|
| 0 | MOTOR CURRENT | `−`/`+` (1 A step, hold to ramp, 0…60 A) and **UPDATE**; `applied: N A` shows what is being sent |
| 1 | SPEED LIMIT | 25 km/h toggle; **OFF** after every power-up (not persisted) |
| 2 | Speedometer | Ø190 needle scale, blue power ring (`voltage × input current`, red/full above `POWER_ALERT_W` = 3 kW), speed + kW, charge %/voltage/temperature, mode badges (`LIMIT 25`, `I 12 A`, `LOCKED`) |
| 3 | Report | `TRIP (SINCE BOOT)`, `TOTAL`, `COST, ILS`, `MOTOR`/`BATTERY` current, **MEASURE PEAK**, settings gear |
| 4 | LOCK | 1…9 keypad for the code |
| 5 | Diagnostics | only when DEBUG MODE is on: VESC fault by name (`mc_fault_code`), latched fault since boot, duty, motor temp, `VESC LINK: OK / NO DATA` |

- Double tap on the dial toggles the 25 km/h limit without paging.
- **MEASURE PEAK** switches the current labels and kW caption to peaks (green) from raw unsmoothed values; the ring stays live. Re-enabling restarts the measurement.
- The gear opens a modal above the UI; its only entry, DEBUG MODE, is stored in NVS. LVGL 8.3 cannot remove a tab, so the toggle rebuilds the whole UI (`lv_async_call` → `lv_obj_clean` + `ui_build()`) — `ui_build()` is re-entrant and restores button states. Unrelated to the compile-time `DEBUG_MODE` in [main/logger.h](main/logger.h).

## Threading

- **lvgl task** (`Lvgl_Start_Task()`, core 0, priority 2) runs `lv_timer_handler()` every 5–20 ms.
- **`loop()`** (core 1) only polls the VESC every 200 ms — no `delay()`, which used to freeze the UI.
- LVGL is not thread-safe: wrap every `ui_set_*`/`lv_*` call from `loop()` in `Lvgl_Lock()`/`Lvgl_Unlock()` (recursive mutex).
- `full_refresh` is off — partial redraws only (a full frame is 134 KB over SPI).

## Configuration

- [main/ui.cpp](main/ui.cpp): `POLE_PAIRS`, `WHEEL_DIAMETER_M`, `SPEED_MAX_KMH`, `SPEED_ALERT_KMH`, `METER_SIZE`, `CURRENT_STEP_A`, `CURRENT_MAX_A`, `POWER_MAX_W`, `POWER_ALERT_W`, `LOCK_CODE`.
- [main/main.ino](main/main.ino): `ELECTRICITY_RATE_ILS_PER_KWH`, `TARGET_KMH`, `NO_LIMIT_ERPM`, `LIMIT_REFRESH_MS`, `LOCK_BRAKE_A`, `VESC_POLL_INTERVAL_MS`.
- Serial logging: `DEBUG_MODE` in [main/logger.h](main/logger.h).
- NVS namespace `settings`: `tacho_off`, `cost_off` (written at most every 30 s), `lock`, `dbg` (written immediately). The 25 km/h limit is deliberately not persisted.

## PC UI simulator

Local SDL2 build of the same [main/ui.cpp](main/ui.cpp) — no flashing needed. LVGL v8.3.9 and `lv_drivers` (release/v8.3) are fetched automatically via CMake `FetchContent`.

```bash
brew install cmake sdl2 pkg-config
cd simulator
cmake -S . -B build      # first run downloads deps (~30 s)
cmake --build build -j
./build/vesc_ui_sim      # 240x280 window, mouse drag = touch, fake VESC data
```

Headless tab render:

```bash
cmake --build build --target vesc_ui_shot
./build/vesc_ui_shot /tmp/shots     # BMP per tab + settings overlay
```

[simulator/screenshot.c](simulator/screenshot.c) also scripts touches through the lock keypad and the DEBUG MODE toggle and prints the resulting state — the cheapest check that the UI-rebuild path does not crash.

- [simulator/main.c](simulator/main.c) — entry point, LVGL+SDL init, fake VESC driver.
- [simulator/CMakeLists.txt](simulator/CMakeLists.txt) — FetchContent build.
- [simulator/lv_drv_conf.h](simulator/lv_drv_conf.h) — SDL settings; resolution must match `LCD_WIDTH`/`LCD_HEIGHT`. `SDL_ZOOM`: `1` → 240x280, `3` → 720x840, `4` → 960x1120 (rebuild after changing).
- [simulator/arduino_shim.cpp](simulator/arduino_shim.cpp) — Arduino-free copies of `calcBatteryPercent` and `erpm_to_kmh`.

## License

MIT.
