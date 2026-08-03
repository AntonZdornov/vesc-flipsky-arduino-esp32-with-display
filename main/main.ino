#include <Arduino.h>
#include <HardwareSerial.h>
#include "VescUart.h"
#include "display.h"
#include "lvgl_driver.h"
#include "logger.h"
#include "ui.h"
#include "vesc_limit.h"
#include <lvgl.h>
#include <Preferences.h>

Preferences prefs;

unsigned long lastFetch = 0;
static const unsigned long VESC_POLL_INTERVAL_MS = 200;  // how often the VESC is polled

// The speed limit is toggled by a button on the left UI tab (ui_get_limit25()).
// It is always off after power-up - the state is not persisted.
static const float TARGET_KMH = 25.0f;  // speed limit
// The limit is implemented as a temporary Max ERPM in the VESC config (see vesc_limit.h):
// setRPM over UART did not work because the throttle app inside the VESC overwrites it.
// WARNING - the "no limit" ERPM must be NO LOWER than the Max ERPM configured in VESC
// Tool: this is the value pushed to the controller when the limit is turned off (until
// the controller reboots).
static const float NO_LIMIT_ERPM = 100000.0f;
static const unsigned long LIMIT_REFRESH_MS = 3000;  // re-send so it survives a VESC reboot
static bool limit_sent_state = false;                // which mode has already been sent to the VESC
static bool lock_sent_state = false;                 // which lock mode has already been sent
static unsigned long last_limit_send_ms = 0;

// Lock (keypad tab, code in ui.cpp). Drive current is taken away inside the VESC via
// l_current_max_scale = 0, otherwise the throttle app would beat any command we send.
// setBrakeCurrent is layered on top as a holding brake and, like setCurrent, has to be
// repeated every cycle (the command times out after ~1 s).
static const float LOCK_BRAKE_A = 6.0f;
static bool lock_saved_state = false;   // what is already stored in NVS
static bool debug_saved_state = false;

// Board: Waveshare ESP32-S3-Touch-LCD-1.69.
// GPIO0 is the BOOT button (the only free button on the board).
// GPIO2/GPIO3 are free pads on the expansion header (5V/3V3/GND are there too).
// Alternatives for the UART: GPIO17/GPIO18 or the U0RXD=44 / U0TXD=43 pads,
// but UART0 is taken by logging (see DEBUG_MODE in logger.h).
#define BTN_PIN 0
#define VESC_RX_PIN 44  // VESC RX (GPIO3 pad)
#define VESC_TX_PIN 43  // VESC TX (GPIO2 pad)
#define BAUD 115200
HardwareSerial VSerial(1);  // create the second UART
VescUart UART;

// Odometer: Total accumulates across runs in NVS, Trip counts from the current boot
static const char *PREFS_NAMESPACE = "settings";
static const char *PREFS_KEY_TACHO_OFFSET = "tacho_off";
static const char *PREFS_KEY_COST_OFFSET = "cost_off";
// The lock and the diagnostics mode must survive a power cycle
static const char *PREFS_KEY_LOCK = "lock";
static const char *PREFS_KEY_DEBUG = "dbg";
static const unsigned long PERSIST_SAVE_INTERVAL_MS = 30000;  // write to NVS at most once every 30 s
static float tacho_offset_persisted = 0.0f;  // offset loaded from NVS (raw VESC counts)
static float boot_tacho = 0.0f;              // first VESC reading after boot
static bool boot_tacho_set = false;
static float last_saved_total = 0.0f;
static unsigned long last_persist_save_ms = 0;

// Electricity cost: computed from the VESC telemetry (wattHours - wattHoursCharged).
// Battery: 21 Ah x 48 V = about 1.008 kWh per full charge (FYI, not used in the math).
static const float ELECTRICITY_RATE_ILS_PER_KWH = 0.68f;
static float cost_offset_persisted = 0.0f;   // accumulated cost loaded from NVS, ILS
static float boot_net_wh = 0.0f;             // (wattHours - wattHoursCharged) at the first reading
static bool boot_net_wh_set = false;
static float last_saved_cost = 0.0f;

void setup() {
  LOG_BEGIN(BAUD);
  prefs.begin(PREFS_NAMESPACE, false);
  // Persistent total odometer (stored as a sum of raw VESC tachometerAbs counts)
  tacho_offset_persisted = prefs.getFloat(PREFS_KEY_TACHO_OFFSET, 0.0f);
  last_saved_total = tacho_offset_persisted;
  // Persistent accumulated electricity cost (in shekels)
  cost_offset_persisted = prefs.getFloat(PREFS_KEY_COST_OFFSET, 0.0f);
  last_saved_cost = cost_offset_persisted;
  // prefs stays open for the whole run - we save periodically from loop()
  // The diagnostics mode is read BEFORE building the UI: ui_build() fixes the tab set
  ui_set_debug_enabled(prefs.getBool(PREFS_KEY_DEBUG, false));
  debug_saved_state = ui_get_debug_enabled();
  pinMode(BTN_PIN, INPUT_PULLUP);
  delay(200);
  LCD_Init();
  Lvgl_Init();
  ui_build();
  // The lock is restored after the build - ui_set_lock() touches widgets.
  // The clamp is not sent to the VESC yet: the first loop() iteration does that.
  ui_set_lock(prefs.getBool(PREFS_KEY_LOCK, false));
  lock_saved_state = ui_get_lock();
  // The UI moves into its own FreeRTOS task: swipes and animations no longer depend
  // on how long loop() waits for a VESC reply
  Lvgl_Start_Task();

  VSerial.begin(BAUD, SERIAL_8N1, VESC_RX_PIN, VESC_TX_PIN);
  UART.setSerialPort(&VSerial);

  delay(200);
}

void loop() {
  // LVGL runs in its own task (Lvgl_Start_Task) - nothing to pump from here.
  const unsigned long now_ms = millis();
  if (now_ms - lastFetch < VESC_POLL_INTERVAL_MS) {
    vTaskDelay(pdMS_TO_TICKS(10));  // yield the CPU to the UI task
    return;
  }
  lastFetch = now_ms;

  const bool lock_on = ui_get_lock();

  int state = digitalRead(BTN_PIN);  // read the state

  // While the lock is on we send no manual drive commands - they would fight the brake
  if (state == LOW && !lock_on) {
    LOG_PRINTLN("Button held: test duty");
    UART.setDuty(0.03f);
  }

  // The speed limit and the lock patch the same mcconf packet (it rewrites all fields
  // at once), so they are folded into a single struct. It is sent on any change, and
  // while anything is active it is repeated every LIMIT_REFRESH_MS (store=0, the values
  // do not survive a controller reboot). Before getVescValues(), so it does not disturb
  // reading the reply.
  const bool limit_on = ui_get_limit25();
  const bool guard_on = limit_on || lock_on;
  // At boot nothing is sent when the lock is off and the limit is off: the restored
  // states match *_sent_state and the VESC config stays as configured in VESC Tool.
  // A lock restored from NVS, however, is pushed to the controller on the first iteration.
  if (limit_on != limit_sent_state || lock_on != lock_sent_state ||
      (guard_on && now_ms - last_limit_send_ms >= LIMIT_REFRESH_MS)) {
    VescLimits lim;
    lim.max_erpm = limit_on ? ui_kmh_to_erpm(TARGET_KMH) : NO_LIMIT_ERPM;
    lim.min_erpm = -lim.max_erpm;
    lim.current_min_scale = 1.0f;             // do not clamp the braking side: it holds the lock
    lim.current_max_scale = lock_on ? 0.0f : 1.0f;  // 0 = the throttle produces no current
    vesc_send_limits(VSerial, lim);
    limit_sent_state = limit_on;
    lock_sent_state = lock_on;
    last_limit_send_ms = now_ms;
  }

  // The lock and diagnostics states live in NVS: these events are rare, so they are
  // written immediately, without the odometer's 30-second throttling.
  if (lock_on != lock_saved_state) {
    prefs.putBool(PREFS_KEY_LOCK, lock_on);
    lock_saved_state = lock_on;
    LOG_PRINTF("lock %s (saved)\n", lock_on ? "ON" : "OFF");
  }
  const bool debug_on = ui_get_debug_enabled();
  if (debug_on != debug_saved_state) {
    prefs.putBool(PREFS_KEY_DEBUG, debug_on);
    debug_saved_state = debug_on;
  }

  // The lock's holding brake - before reading telemetry and regardless of whether the
  // VESC replied: the lock must hold even when the link fails.
  if (lock_on) UART.setBrakeCurrent(LOCK_BRAKE_A);

  if (UART.getVescValues()) {
    float voltage = UART.data.inpVoltage;
    float temp = UART.data.tempMosfet;
    float rpm = UART.data.rpm;
    float tachometerAbs = UART.data.tachometerAbs;
    float motorCurrent = UART.data.avgMotorCurrent;  // motor phase current, A
    float inputCurrent = UART.data.avgInputCurrent;  // current drawn from the battery, A
    float wattHours = UART.data.wattHours;
    float wattHoursCharged = UART.data.wattHoursCharged;
    float net_wh = wattHours - wattHoursCharged;  // net battery consumption in Wh

    // Anchor the baseline to the first valid VESC reading
    if (!boot_tacho_set) {
      boot_tacho = tachometerAbs;
      boot_tacho_set = true;
    }
    if (!boot_net_wh_set) {
      boot_net_wh = net_wh;
      boot_net_wh_set = true;
    }

    float trip_tacho = tachometerAbs - boot_tacho;
    float total_tacho = tacho_offset_persisted + trip_tacho;

    float trip_kwh = (net_wh - boot_net_wh) / 1000.0f;
    float total_cost = cost_offset_persisted + trip_kwh * ELECTRICITY_RATE_ILS_PER_KWH;

    // LVGL is not thread-safe: draw only while holding the UI task's mutex
    if (Lvgl_Lock(100)) {
      ui_set_tachometerAbs(total_tacho);
      ui_set_tachometer(trip_tacho);
      ui_set_cost(total_cost);
      ui_set_currents(motorCurrent, inputCurrent);
      ui_set_power(voltage * inputCurrent);  // wheel power, W
      ui_set_battery(voltage);
      ui_set_temp(temp);
      ui_set_speed(rpm);
      ui_set_diag((int)UART.data.error, UART.data.dutyCycleNow, UART.data.tempMotor, true);
      Lvgl_Unlock();
    } else {
      LOG_PRINTLN("LVGL lock timeout, skip UI update");
    }

    // The current from tab 0 is not sent while locked - it would pull forward against the brake
    const float set_current = ui_get_current_applied();
    if (!lock_on && set_current > 0.01f) {
      UART.setCurrent(set_current);  // repeat every cycle: the VESC times out after ~1 s
      LOG_PRINTF("setCurrent %.1f A\n", set_current);
    }

    unsigned long now = millis();
    if (now - last_persist_save_ms >= PERSIST_SAVE_INTERVAL_MS) {
      if (total_tacho != last_saved_total) {
        prefs.putFloat(PREFS_KEY_TACHO_OFFSET, total_tacho);
        last_saved_total = total_tacho;
      }
      if (total_cost != last_saved_cost) {
        prefs.putFloat(PREFS_KEY_COST_OFFSET, total_cost);
        last_saved_cost = total_cost;
      }
      last_persist_save_ms = now;
    }
  } else {
    // No reply - the diagnostics tab shows this as "VESC LINK: NO DATA"
    if (Lvgl_Lock(100)) {
      ui_set_diag(0, 0.0f, 0.0f, false);
      Lvgl_Unlock();
    }
  }

  // No delay(200): the pace is set by VESC_POLL_INTERVAL_MS and the UI lives in its own task
  vTaskDelay(pdMS_TO_TICKS(10));
}
