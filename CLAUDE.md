# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this project actually is

An Arduino sketch for a **Waveshare ESP32-S3-Touch-LCD-1.69** board (ESP32-S3R8, 8 MB PSRAM) with a built-in **ST7789V2 240x280 SPI LCD** — driven in **portrait**, so the logical resolution is **240x280** — acting as a display/dashboard for a **Flipsky VESC** (electric vehicle motor controller). The ESP32 talks to the VESC over UART (`VescUart` library) and renders speed, battery %, voltage, MOSFET temperature, and trip/total odometer with **LVGL v8.3.9**. There is also scaffolding for **NimBLE** (configuring Wi-Fi credentials over BLE) and a **WS2812 NeoPixel** indicator, but those are not wired into `setup()`/`loop()` in [main/main.ino](main/main.ino) right now — the active code path is VESC → LVGL only.

The onboard CST816T touch **is** used (swipe between tabs). The IMU, RTC, buzzer and battery ADC are not; there is no onboard addressable LED on this board.

## Build / flash

There is no `platformio.ini`, `Makefile`, or `arduino-cli.yaml` in the repo. The project is built and flashed via the **Arduino IDE** (or `arduino-cli`) targeting **ESP32S3 Dev Module** (esp32 core 3.x — `ledcAttach()` is used; Flash 16MB, OPI PSRAM).

External configuration the user maintains outside the repo:
- The Arduino libraries `VescUart`, `lvgl` (v8.3.9), `NimBLEDevice`, and `Adafruit_NeoPixel` must be installed in the IDE.
- LVGL is configured via [configs/lv_conf.h](configs/lv_conf.h) — this file lives outside the sketch folder and must be discoverable on the LVGL include path (Arduino IDE: place at the libraries root next to the `lvgl` folder, or add the `configs/` directory to the include search path).
- The sketch folder is [main/](main/) and the entry point is [main/main.ino](main/main.ino) — open that file in the IDE.

There are no tests and no linters configured.

## Architecture (the parts you need to read multiple files to understand)

**Display stack — three layers, each in its own file:**
1. [main/display.cpp](main/display.cpp) is a **hand-rolled ST7789 driver** over the Arduino `SPI` library. It owns pin assignments (DC=4, CS=5, SCLK=6, MOSI=7, RST=8, BL=15, MISO unused — see [main/display.h](main/display.h)), the panel init sequence, and `LCD_addWindow()` which blits a pixel buffer to a rectangle. Orientation is a single switch — `LCD_PORTRAIT` in [main/display.h](main/display.h) — which picks resolution, MADCTL and the window offsets together: portrait (current) = 240x280 / `0x00` / `Offset_X=0, Offset_Y=20`; landscape = 280x240 / `0x60` / `Offset_X=20, Offset_Y=0`. The offsets exist because the 240x280 panel sits inside the controller's 240x320 memory starting at row 20; they are expressed in the *logical* frame, so `LCD_SetCursor()` maps X→CASET, Y→RASET directly. Note that `ui_build()` is laid out for **portrait only** — flipping the switch needs a UI rework, not just a driver change. SPI runs at 40 MHz because GPIO6/7 are not FSPI IOMUX pins on the S3.
2. [main/lvgl_driver.cpp](main/lvgl_driver.cpp) bridges LVGL to that driver: it allocates two `lv_color_t` framebuffers sized `WIDTH * HEIGHT / 20`, registers `Lvgl_Display_LCD` as the flush callback (which calls `LCD_addWindow`), wires `Lvgl_Touchpad_Read` to the CST816T, and starts an `esp_timer` that ticks LVGL every 5 ms. `full_refresh` is deliberately **0** (partial redraws only — a full 240x280 frame is 134 KB over 40 MHz SPI).
3. [main/touch.cpp](main/touch.cpp) is a minimal CST816T driver over `Wire` (addr 0x15, SCL=10/SDA=11, RST=13, INT=14) — polled from LVGL's `read_cb`, no interrupt. `Touch_Init()` writes reg `0xFE = 1` to disable the chip's auto-sleep, otherwise the first touches are lost. Coordinates come in the native portrait frame, so they pass through unchanged while `LCD_PORTRAIT == 1`.
4. [main/ui.cpp](main/ui.cpp) builds the widget tree in `ui_build()`: an `lv_tabview` (tab buttons hidden, `LV_DIR_TOP` with size 0) with four swipeable pages — **0** motor-current `−`/`+`/UPDATE, **1** speed-limit toggle, **2** the speedometer dash (active at boot), **3** the trip/total/cost report — plus a `TAB_COUNT`-dot page indicator parented to the screen. Exposes `ui_set_*` setters, `ui_get_limit25()`/`ui_set_limit25()`, `ui_get_current_applied()` and `ui_speed_kmh()`/`ui_kmh_to_erpm()`. Adding a tab means bumping the `TAB_*` indices and `TAB_COUNT` together — the dots array is sized from it.

**Threading (this matters):** [main/lvgl_driver.cpp](main/lvgl_driver.cpp) runs `lv_timer_handler()` in its own FreeRTOS task (`Lvgl_Start_Task()`, core 0, priority 2, 5–20 ms period) so rendering and swipe inertia don't depend on `loop()`. `loop()` (core 1) only polls the VESC every `VESC_POLL_INTERVAL_MS` and must wrap **every** `lv_*`/`ui_set_*` call in `Lvgl_Lock()`/`Lvgl_Unlock()` (recursive mutex) — LVGL is not thread-safe. Never add `delay()` to `loop()` for pacing; use the millis() guard that's already there. `Timer_Loop()` still exists for compatibility but must not be called when the task is running.

**VESC data flow:**
`UART.getVescValues()` in `loop()` polls the VESC over `HardwareSerial(1)` on pins RX=3 / TX=2 at 115200 baud. The sketch reads `inpVoltage`, `tempMosfet`, `rpm`, `tachometer`, `tachometerAbs` and pushes them into the LVGL labels. eRPM ↔ km/h conversion uses `POLE_PAIRS` and `WHEEL_CIRC_M` defined at the top of [main/ui.cpp](main/ui.cpp) — these are mechanical constants for the user's specific vehicle and **must be re-checked** if changing motor or wheel.

⚠️ Note: `POLE_PAIRS` is defined **twice** with different values — `15` in [main/ui.cpp](main/ui.cpp#L40) (used for display calculations) and `14` in [main/utils.cpp](main/utils.cpp#L5) (used by `kmh_to_erpm` only). They are not unified. If you touch either, decide whether to consolidate.

**Speed limit (25 km/h):** owned by the UI toggle on tab 0, state in a `volatile bool` inside [main/ui.cpp](main/ui.cpp), deliberately **not persisted** — it must come up OFF after every power cycle. Enforcement lives in [main/vesc_limit.cpp](main/vesc_limit.cpp): on a toggle change `loop()` sends a raw `COMM_SET_MCCONF_TEMP` packet that rewrites the VESC's `l_min_erpm`/`l_max_erpm` to ±`ui_kmh_to_erpm(TARGET_KMH)`, so the controller itself clamps and the throttle app cannot exceed it. `store = 0`, so the change is RAM-only — hence the re-send every `LIMIT_REFRESH_MS` while the limit is ON (survives a VESC reboot). Turning the toggle off sends ±`NO_LIMIT_ERPM` (100000), which **overwrites** whatever Max ERPM is configured in VESC Tool until the VESC reboots — raise that constant if the real config is higher. Nothing is sent at boot, so an untouched toggle leaves the stored config alone.

An earlier implementation used `UART.setRPM()` while above the threshold and **did not work**: the ADC/PPM throttle app inside the VESC rewrites the setpoint every control cycle (~1 kHz) and simply overwrites 5 Hz UART commands. Don't go back to it. The other `UART.set*` commands (current, duty) are unaffected because they're only used when the throttle isn't fighting them.

**Motor current (tab 0):** (the speed limit no longer suppresses it — the clamp happens inside the VESC) `−`/`+` move a `current_setpoint` that is *not* sent anywhere; pressing UPDATE copies it into `current_applied`, which `loop()` re-sends via `UART.setCurrent()` on **every** poll — a single send would decay after the VESC's ~1 s command timeout, so the resend is load-bearing, not redundant. Zero means "send nothing". The button is a **toggle**: while `current_applied` equals `current_setpoint` (and is non-zero) it renders as a red `OFF` and the next press zeroes `current_applied` — the VESC's command timeout then hands the throttle back. Changing the setpoint with `−`/`+` re-arms it back to a green `UPDATE`, so the label/colour is derived state, owned by `ui_refresh_current()`, not a separate flag.

**Button (GPIO 0):** while held LOW, the loop calls `UART.setDuty(0.03f)` — a hardcoded test/throttle gesture, unrelated to the speed limit and to the current tab.

**BLE / Wi-Fi / LED:** [main/ble_service.cpp](main/ble_service.cpp), [main/wifi_service.cpp](main/wifi_service.cpp), [main/led.cpp](main/led.cpp) implement a "receive Wi-Fi credentials over BLE, then connect" flow with an (external) NeoPixel indicator on GPIO 17 — GPIO 8 is LCD_RST on this board. **None of these are called from `setup()`** — they're dormant. If a task asks to "enable BLE" or "turn on Wi-Fi", you'll need to wire `initBLE()` / `leds_init()` into `setup()` yourself.

## Conventions worth knowing

- **Logging is gated by `DEBUG_MODE` in [main/logger.h](main/logger.h).** Set it to `1` to enable `Serial`-based `LOG_*` macros; `0` makes them no-ops (the default). `DEBUG_MODE == 1` also adds a debug label to the report tab in `ui_build()`.
- **Comments and identifiers are mixed Russian/English.** Preserve existing Russian comments when editing nearby code.
- **`Preferences` (NVS)** is opened in `setup()` for persistent settings but currently nothing is read or written — it's scaffolding.
- LVGL is used in **immediate-mode flex layout** style (no LVGL screen-loading, no styles factored out). Add new widgets by extending `ui_build()` and adding a `ui_set_*` helper that the loop calls.
