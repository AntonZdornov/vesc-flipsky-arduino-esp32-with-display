# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this project actually is

An Arduino sketch for an **ESP32-C6** board with a built-in **ST7789 320x172 SPI LCD**, acting as a display/dashboard for a **Flipsky VESC** (electric vehicle motor controller). The ESP32 talks to the VESC over UART (`VescUart` library) and renders speed, battery %, voltage, MOSFET temperature, and trip/total odometer with **LVGL v8.3.9**. There is also scaffolding for **NimBLE** (configuring Wi-Fi credentials over BLE) and a **WS2812 NeoPixel** indicator, but those are not wired into `setup()`/`loop()` in [main/main.ino](main/main.ino) right now — the active code path is VESC → LVGL only.

The [README.md](README.md) describes a different project ("ELM327 CAN to BLE Bridge" / Toyota hybrid SOC) — that text is **stale** and does not match the code. Don't trust it; trust [main/main.ino](main/main.ino).

## Build / flash

There is no `platformio.ini`, `Makefile`, or `arduino-cli.yaml` in the repo. The project is built and flashed via the **Arduino IDE** (or `arduino-cli`) targeting an ESP32-C6 board.

External configuration the user maintains outside the repo:
- The Arduino libraries `VescUart`, `lvgl` (v8.3.9), `NimBLEDevice`, and `Adafruit_NeoPixel` must be installed in the IDE.
- LVGL is configured via [configs/lv_conf.h](configs/lv_conf.h) — this file lives outside the sketch folder and must be discoverable on the LVGL include path (Arduino IDE: place at the libraries root next to the `lvgl` folder, or add the `configs/` directory to the include search path).
- The sketch folder is [main/](main/) and the entry point is [main/main.ino](main/main.ino) — open that file in the IDE.

There are no tests and no linters configured.

## Architecture (the parts you need to read multiple files to understand)

**Display stack — three layers, each in its own file:**
1. [main/display.cpp](main/display.cpp) is a **hand-rolled ST7789 driver** over the Arduino `SPI` library. It owns pin assignments (CS=14, DC=15, RST=21, BL=22, SCLK=7, MOSI=6, MISO=5 — see [main/display.h](main/display.h)), the panel init sequence, and `LCD_addWindow()` which blits a pixel buffer to a rectangle. The `Offset_Y = 34` in [main/display.h](main/display.h#L22) is the ST7789 row offset for the 172-pixel-tall variant — do not change without re-checking the panel datasheet.
2. [main/lvgl_driver.cpp](main/lvgl_driver.cpp) bridges LVGL to that driver: it allocates two `lv_color_t` framebuffers sized `WIDTH * HEIGHT / 20`, registers `Lvgl_Display_LCD` as LVGL's flush callback (which calls `LCD_addWindow`), and starts an `esp_timer` that ticks LVGL every 5 ms. `Timer_Loop()` (called from `loop()`) drives `lv_timer_handler()`.
3. [main/main.ino](main/main.ino) builds the LVGL widget tree in `ui_build()` (a flex column: a 3-column body for battery/speed/temp, a footer for trip/total odometer) and exposes `ui_set_*` setters that the main loop calls each tick.

**VESC data flow:**
`UART.getVescValues()` in `loop()` polls the VESC over `HardwareSerial(1)` on pins RX=3 / TX=2 at 115200 baud. The sketch reads `inpVoltage`, `tempMosfet`, `rpm`, `tachometer`, `tachometerAbs` and pushes them into the LVGL labels. eRPM ↔ km/h conversion uses `POLE_PAIRS` and `WHEEL_CIRC_M` defined at the top of [main/main.ino](main/main.ino) — these are mechanical constants for the user's specific vehicle and **must be re-checked** if changing motor or wheel.

⚠️ Note: `POLE_PAIRS` is defined **twice** with different values — `15` in [main/main.ino](main/main.ino#L34) (used for display calculations) and `14` in [main/utils.cpp](main/utils.cpp#L5) (used by `kmh_to_erpm` only). They are not unified. If you touch either, decide whether to consolidate.

**Button (GPIO 0):** while held LOW, the loop calls `UART.setDuty(0.03f)` — a hardcoded test/throttle gesture, not a real speed limiter. The `limit25_enabled` / `TARGET_KMH` / `kmh_to_erpm()` machinery is plumbed but the actual rate-limit branch is commented out.

**BLE / Wi-Fi / LED:** [main/ble_service.cpp](main/ble_service.cpp), [main/wifi_service.cpp](main/wifi_service.cpp), [main/led.cpp](main/led.cpp) implement a "receive Wi-Fi credentials over BLE, then connect" flow with a NeoPixel indicator on GPIO 8. **None of these are called from `setup()`** — they're dormant. If a task asks to "enable BLE" or "turn on Wi-Fi", you'll need to wire `initBLE()` / `leds_init()` into `setup()` yourself.

## Conventions worth knowing

- **Logging is gated by `DEBUG_MODE` in [main/logger.h](main/logger.h).** Set it to `1` to enable `Serial`-based `LOG_*` macros; `0` makes them no-ops (the default). `DEBUG_MODE == 1` also swaps the footer odometer labels for a debug label in `ui_build()`.
- **Comments and identifiers are mixed Russian/English.** Preserve existing Russian comments when editing nearby code.
- **`Preferences` (NVS)** is opened in `setup()` for persistent settings but currently nothing is read or written — it's scaffolding.
- LVGL is used in **immediate-mode flex layout** style (no LVGL screen-loading, no styles factored out). Add new widgets by extending `ui_build()` and adding a `ui_set_*` helper that the loop calls.
