# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this project actually is

An Arduino sketch for a **Waveshare ESP32-S3-Touch-LCD-1.69** board (ESP32-S3R8, 8 MB PSRAM) with a built-in **ST7789V2 240x280 SPI LCD** — driven in **portrait**, so the logical resolution is **240x280** — acting as a display/dashboard for a **Flipsky VESC** (electric vehicle motor controller). The ESP32 talks to the VESC over UART (`VescUart` library) and renders speed, battery %, voltage, MOSFET temperature, and trip/total odometer with **LVGL v8.3.9**. The active code path is VESC → LVGL only; the old dormant BLE / Wi-Fi / NeoPixel scaffolding has been deleted, so there is no BLE, Wi-Fi or LED code in the sketch.

The onboard CST816T touch **is** used (swipe between tabs). The IMU, RTC, buzzer and battery ADC are not; there is no onboard addressable LED on this board.

## Build / flash

There is no `platformio.ini`, `Makefile`, or `arduino-cli.yaml` in the repo. The project is built and flashed via the **Arduino IDE** (or `arduino-cli`) targeting **ESP32S3 Dev Module** (esp32 core 3.x — `ledcAttach()` is used; Flash 16MB, OPI PSRAM).

External configuration the user maintains outside the repo:
- The Arduino libraries `VescUart` and `lvgl` (v8.3.9) must be installed in the IDE.
- LVGL is configured via [configs/lv_conf.h](configs/lv_conf.h) — this file lives outside the sketch folder and must be discoverable on the LVGL include path (Arduino IDE: place at the libraries root next to the `lvgl` folder, or add the `configs/` directory to the include search path).
- The sketch folder is [main/](main/) and the entry point is [main/main.ino](main/main.ino) — open that file in the IDE.

There are no tests and no linters configured. There is, however, a PC build of the UI under [simulator/](simulator/) (CMake + SDL2, fetches LVGL v8.3.9 itself, compiles the real [main/ui.cpp](main/ui.cpp) against [simulator/arduino_shim.cpp](simulator/arduino_shim.cpp)) — use it to check any UI change before flashing:

```
cmake -S simulator -B simulator/build && cmake --build simulator/build   # SDL window
cmake --build simulator/build --target vesc_ui_shot && \
  simulator/build/vesc_ui_shot <outdir>                                  # headless
```

`vesc_ui_shot` ([simulator/screenshot.c](simulator/screenshot.c)) renders every tab plus the settings overlay to 240x280 BMPs without a window, then scripts a synthetic touch through the lock keypad and the debug toggle and prints the resulting state — that is the cheapest way to smoke-test the UI-rebuild path.

## Architecture (the parts you need to read multiple files to understand)

**Display stack — three layers, each in its own file:**
1. [main/display.cpp](main/display.cpp) is a **hand-rolled ST7789 driver** over the Arduino `SPI` library. It owns pin assignments (DC=4, CS=5, SCLK=6, MOSI=7, RST=8, BL=15, MISO unused — see [main/display.h](main/display.h)), the panel init sequence, and `LCD_addWindow()` which blits a pixel buffer to a rectangle. Orientation is a single switch — `LCD_PORTRAIT` in [main/display.h](main/display.h) — which picks resolution, MADCTL and the window offsets together: portrait (current) = 240x280 / `0x00` / `Offset_X=0, Offset_Y=20`; landscape = 280x240 / `0x60` / `Offset_X=20, Offset_Y=0`. The offsets exist because the 240x280 panel sits inside the controller's 240x320 memory starting at row 20; they are expressed in the *logical* frame, so `LCD_SetCursor()` maps X→CASET, Y→RASET directly. Note that `ui_build()` is laid out for **portrait only** — flipping the switch needs a UI rework, not just a driver change. SPI runs at 40 MHz because GPIO6/7 are not FSPI IOMUX pins on the S3.
2. [main/lvgl_driver.cpp](main/lvgl_driver.cpp) bridges LVGL to that driver: it allocates two `lv_color_t` framebuffers sized `WIDTH * HEIGHT / 20`, registers `Lvgl_Display_LCD` as the flush callback (which calls `LCD_addWindow`), wires `Lvgl_Touchpad_Read` to the CST816T, and starts an `esp_timer` that ticks LVGL every 5 ms. `full_refresh` is deliberately **0** (partial redraws only — a full 240x280 frame is 134 KB over 40 MHz SPI).
3. [main/touch.cpp](main/touch.cpp) is a minimal CST816T driver over `Wire` (addr 0x15, SCL=10/SDA=11, RST=13, INT=14) — polled from LVGL's `read_cb`, no interrupt. `Touch_Init()` writes reg `0xFE = 1` to disable the chip's auto-sleep, otherwise the first touches are lost. Coordinates come in the native portrait frame, so they pass through unchanged while `LCD_PORTRAIT == 1`.
4. [main/ui.cpp](main/ui.cpp) builds the widget tree in `ui_build()`: an `lv_tabview` (tab buttons hidden, `LV_DIR_TOP` with size 0) with swipeable pages — **0** motor-current `−`/`+`/UPDATE, **1** speed-limit toggle, **2** the speedometer dash (active at boot), **3** the trip/total/cost report, **4** the lock keypad, **5** the VESC diagnostics page (built **only** when `debug_enabled`) — plus a page-indicator dot row parented to the screen. Exposes `ui_set_*` setters, `ui_get_limit25()`/`ui_set_limit25()`, `ui_get_lock()`/`ui_set_lock()`, `ui_get_debug_enabled()`/`ui_set_debug_enabled()`, `ui_set_diag()`, `ui_get_current_applied()` and `ui_speed_kmh()`/`ui_kmh_to_erpm()`.
   - The tab count is **runtime** (`tab_count`, 5 or 6), not a `#define`; `TAB_MAX` only sizes the `dots[]` array. Adding a tab means bumping the `TAB_*` indices and `TAB_MAX` together.
   - LVGL 8.3 has no `lv_tabview_remove_tab()`, so toggling the diagnostics tab **rebuilds the whole UI**: `ui_debug_btn_cb()` schedules `ui_rebuild_async_cb()` via `lv_async_call()` (mandatory — you cannot delete the tree from a callback of a button inside it), which cleans `lv_layer_top()` + `lv_scr_act()` and calls `ui_build()` again. Because of that, `ui_build()` is **re-entrant**: it starts with `ui_reset_refs()` (all widget statics back to NULL) and every widget that mirrors persistent state must have its state restored at build time (`ui_set_limit25(limit25_on)`, `ui_refresh_lock()`, the `btn_peak` CHECKED restore) — plain "refresh" is not enough for a freshly created widget.

**Threading (this matters):** [main/lvgl_driver.cpp](main/lvgl_driver.cpp) runs `lv_timer_handler()` in its own FreeRTOS task (`Lvgl_Start_Task()`, core 0, priority 2, 5–20 ms period) so rendering and swipe inertia don't depend on `loop()`. `loop()` (core 1) only polls the VESC every `VESC_POLL_INTERVAL_MS` and must wrap **every** `lv_*`/`ui_set_*` call in `Lvgl_Lock()`/`Lvgl_Unlock()` (recursive mutex) — LVGL is not thread-safe. Never add `delay()` to `loop()` for pacing; use the millis() guard that's already there.

**VESC data flow:**
`UART.getVescValues()` in `loop()` polls the VESC over `HardwareSerial(1)` on pins RX=3 / TX=2 at 115200 baud. The sketch reads `inpVoltage`, `tempMosfet`, `rpm`, `tachometer`, `tachometerAbs` and pushes them into the LVGL labels. eRPM ↔ km/h conversion uses `POLE_PAIRS` and `WHEEL_CIRC_M` defined at the top of [main/ui.cpp](main/ui.cpp) — these are mechanical constants for the user's specific vehicle and **must be re-checked** if changing motor or wheel.

**Speed limit (25 km/h) and lock share one packet.** [main/vesc_limit.cpp](main/vesc_limit.cpp) exposes a single `vesc_send_limits(Stream&, const VescLimits&)` that sends a raw `COMM_SET_MCCONF_TEMP`. That packet rewrites **all eight** limit fields at once (`l_current_min/max_scale`, `l_min/max_erpm`, `l_min/max_duty`, `l_watt_min/max`), so no feature may send it on its own — `loop()` folds every active regulator into one `VescLimits` and sends that. `store = 0`, so the change is RAM-only — hence the re-send every `LIMIT_REFRESH_MS` while anything is active (survives a VESC reboot).

- **Speed limit:** UI toggle on tab 1, `volatile bool` in [main/ui.cpp](main/ui.cpp), deliberately **not persisted** — must come up OFF after every power cycle. Sets `l_min/max_erpm` to ±`ui_kmh_to_erpm(TARGET_KMH)`. Off sends ±`NO_LIMIT_ERPM` (100000), which **overwrites** whatever Max ERPM is configured in VESC Tool until the VESC reboots — raise that constant if the real config is higher.
- **Lock (tab 4, code `1987`):** sets `l_current_max_scale = 0`, so the throttle app can no longer produce drive current, and `loop()` additionally repeats `UART.setBrakeCurrent(LOCK_BRAKE_A)` every poll as a holding brake. `l_current_min_scale` stays at 1.0 on purpose — zeroing it would kill that brake too. `max_erpm = 0` was rejected as the mechanism: it makes the VESC regulate around zero and fight anyone pushing the scooter by hand. The lock **is persisted** (NVS key `lock`) and is re-sent to the VESC on the first `loop()` iteration after boot, unlike the speed limit which sends nothing until touched. It is a "don't let it ride off" measure, not an anti-theft device: the clamp lives in the VESC's RAM and dies with VESC power.

An earlier implementation used `UART.setRPM()` while above the threshold and **did not work**: the ADC/PPM throttle app inside the VESC rewrites the setpoint every control cycle (~1 kHz) and simply overwrites 5 Hz UART commands. Don't go back to it. The other `UART.set*` commands (current, duty) are unaffected because they're only used when the throttle isn't fighting them.

**Motor current (tab 0):** (the speed limit no longer suppresses it — the clamp happens inside the VESC) `−`/`+` move a `current_setpoint` that is *not* sent anywhere; pressing UPDATE copies it into `current_applied`, which `loop()` re-sends via `UART.setCurrent()` on **every** poll — a single send would decay after the VESC's ~1 s command timeout, so the resend is load-bearing, not redundant. Zero means "send nothing". The button is a **toggle**: while `current_applied` equals `current_setpoint` (and is non-zero) it renders as a red `OFF` and the next press zeroes `current_applied` — the VESC's command timeout then hands the throttle back. Changing the setpoint with `−`/`+` re-arms it back to a green `UPDATE`, so the label/colour is derived state, owned by `ui_refresh_current()`, not a separate flag.

**Settings menu / diagnostics:** the gear button on the trip/report tab (`LV_SYMBOL_SETTINGS`, `IGNORE_LAYOUT`, top-right corner of that page) unhides a modal parented to `lv_layer_top()` — above the tabview *and* the dot row. Its only entry, DEBUG MODE, flips `debug_enabled` (NVS key `dbg`), which adds the diagnostics tab via the UI rebuild described above. That tab shows `UART.data.error` by name, a latch of the last non-zero fault since boot (the live code self-clears when the cause goes away), duty, motor temp and a VESC-link indicator, all fed by `ui_set_diag()`. The fault-name table is duplicated in [main/ui.cpp](main/ui.cpp) from `mc_fault_code` in the library's `datatypes.h` — that header is deliberately **not** included, because `ui.cpp` is also compiled by the PC simulator, which has no VescUart on its include path. Keep it in sync manually. This runtime flag is unrelated to the compile-time `DEBUG_MODE` in [main/logger.h](main/logger.h).

**Button (GPIO 0):** while held LOW, the loop calls `UART.setDuty(0.03f)` — a hardcoded test/throttle gesture, unrelated to the speed limit and to the current tab (suppressed while the lock is on).

**No BLE / Wi-Fi / LED:** the radios are unused and there is no LED on the board. The former `ble_service` / `wifi_service` / `led` / `ui_globals` files (a dormant "receive Wi-Fi credentials over BLE" flow plus a WS2812 indicator) were removed. A task asking to "enable BLE" or "turn on Wi-Fi" means writing it from scratch, not un-commenting something.

## Conventions worth knowing

- **Logging is gated by `DEBUG_MODE` in [main/logger.h](main/logger.h).** Set it to `1` to enable `Serial`-based `LOG_*` macros; `0` makes them no-ops (the default). `DEBUG_MODE == 1` also adds a debug label to the report tab in `ui_build()`.
- **Comments and identifiers are mixed Russian/English.** Preserve existing Russian comments when editing nearby code.
- **`Preferences` (NVS)**, namespace `settings`, is opened once in `setup()` and stays open: `tacho_off` / `cost_off` (total odometer and cost, written at most every `PERSIST_SAVE_INTERVAL_MS`) and `lock` / `dbg` (written immediately on change — rare events). `dbg` must be read **before** `ui_build()`, `lock` **after** it (`ui_set_lock()` touches widgets).
- LVGL is used in **immediate-mode flex layout** style (no LVGL screen-loading, no styles factored out). Add new widgets by extending `ui_build()` and adding a `ui_set_*` helper that the loop calls.
