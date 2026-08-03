#include "ui.h"

#include <lvgl.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef ARDUINO
#include "logger.h"
#endif
#ifndef DEBUG_MODE
#define DEBUG_MODE 0
#endif

// Implemented in main/utils.cpp (Arduino build) or simulator/arduino_shim.cpp (PC simulator).
uint8_t calcBatteryPercent(float voltage);
float erpm_to_kmh(long erpm, int pole_pairs, float wheel_circumference_m);

static lv_obj_t *lbl_batt = NULL;
static lv_obj_t *lbl_volt = NULL;
static lv_obj_t *lbl_speed = NULL;
static lv_obj_t *meter_speed = NULL;
static lv_meter_indicator_t *needle_speed = NULL;
static lv_meter_indicator_t *arc_speed = NULL;
static lv_obj_t *arc_power = NULL;
static lv_obj_t *lbl_power = NULL;
static lv_obj_t *lbl_temp_val = NULL;
static lv_obj_t *lbl_tachometer = NULL;
static lv_obj_t *lbl_tachometerAbs = NULL;
static lv_obj_t *lbl_cost = NULL;
static lv_obj_t *lbl_cur_motor = NULL;
static lv_obj_t *lbl_cur_input = NULL;
static lv_obj_t *btn_peak = NULL;
static lv_obj_t *lbl_peak_btn = NULL;
static lv_obj_t *lbl_debbug = NULL;
static lv_obj_t *tabview = NULL;
static lv_obj_t *btn_limit = NULL;
static lv_obj_t *lbl_limit_state = NULL;
static lv_obj_t *lbl_limit_badge = NULL;
static lv_obj_t *lbl_current_val = NULL;
static lv_obj_t *lbl_current_applied = NULL;
static lv_obj_t *lbl_current_badge = NULL;
static lv_obj_t *btn_current_update = NULL;
static lv_obj_t *lbl_current_update = NULL;
static lv_obj_t *lbl_lock_badge = NULL;
static lv_obj_t *lbl_lock_mask = NULL;
static lv_obj_t *lbl_lock_status = NULL;
static lv_obj_t *settings_modal = NULL;
static lv_obj_t *btn_debug = NULL;
static lv_obj_t *lbl_debug_state = NULL;
static lv_obj_t *lbl_diag_fault = NULL;
static lv_obj_t *lbl_diag_last = NULL;
static lv_obj_t *lbl_diag_duty = NULL;
static lv_obj_t *lbl_diag_tmotor = NULL;
static lv_obj_t *lbl_diag_link = NULL;
static lv_obj_t *dots[6] = { NULL, NULL, NULL, NULL, NULL, NULL };  // size = TAB_MAX

// 25 km/h speed limit. Lives in RAM only: it must be OFF every time the board powers
// up (deliberately not stored in NVS).
// volatile: written from the LVGL task (button callback), read from loop()
static volatile bool limit25_on = false;

// Lock: while it is on, loop() takes drive current away from the throttle inside the
// VESC itself and holds the brake. Unlike the 25 km/h limit the state is stored in NVS
// (otherwise the lock would be pointless) - loop() writes it, this is just the flag.
// volatile: written from the LVGL task.
static volatile bool lock_on = false;
// Diagnostics mode: adds the last tab. Also lives in NVS.
static volatile bool debug_enabled = false;

// Motor current (setCurrent): the setpoint is moved by the +/- buttons, while applied
// is committed only by the UPDATE button - that is what loop() repeats to the VESC.
static volatile float current_setpoint = 0.0f;
static volatile float current_applied = 0.0f;

// Peak current measurement (report tab). While the button is on, ui_set_currents()
// accumulates the maxima and shows them INSTEAD of the live values - in the same
// labels, only the colour differs. Turning the button on restarts the measurement.
// Not written to NVS - this is a one-off measurement, not needed after a reboot.
static volatile bool peak_measuring = false;
static volatile float peak_motor_a = 0.0f;
static volatile float peak_input_a = 0.0f;
// Peak power is accumulated by the same measurement: the button is on the report tab
// but the value is shown on the speedometer, in the kW label instead of the live one.
static volatile float peak_power_w = 0.0f;

// Smoothing of the live currents: the VESC averages them only over a PWM period, so
// the raw values jitter on screen. The coefficient is gentler than the speedometer's -
// these are plain numbers, no needle. Peaks are still taken from the raw values
// (see ui_set_currents).
static float cur_motor_ema = 0.0f;
static float cur_input_ema = 0.0f;
static bool cur_ema_init = false;

// Smoothing of the power: it is computed from the input current, which jitters the
// most on screen (a product of two noisy values).
static float power_ema = 0.0f;
static bool power_ema_init = false;

extern const lv_font_t lv_font_montserrat_48;
extern const lv_font_t lv_font_montserrat_38;
extern const lv_font_t lv_font_montserrat_26;
extern const lv_font_t lv_font_montserrat_22;
extern const lv_font_t lv_font_montserrat_18;
extern const lv_font_t lv_font_montserrat_14;
extern const lv_font_t lv_font_montserrat_12;

#define POLE_PAIRS 15  // usually 7 for a 14-pole motor
#define WHEEL_DIAMETER_M 0.255
#define WHEEL_CIRC_M (3.1415926f * WHEEL_DIAMETER_M)  // circumference in metres
#define TACHO_COUNTS_PER_REV 8192.0f
#define SPEED_MAX_KMH 55       // upper end of the speedometer scale
#define SPEED_ALERT_KMH 50.0f  // above this threshold the speedometer turns red
#define METER_SIZE 190         // speedometer diameter, px (fits into the 240 px width)
// Power ring: drawn along the outer edge of the speedometer, so the scale (the dial)
// is pushed inwards by METER_PAD = ring width + gap.
#define POWER_ARC_W 6          // blue ring thickness, px
#define METER_PAD (POWER_ARC_W + 5)
#define POWER_MAX_W 3000       // top of the ring scale, W (full ring)
#define POWER_ALERT_W 3000.0f  // above this threshold the ring and the number turn red
#define COLOR_POWER lv_color_hex(0x2196f3)

#define COLOR_ACCENT lv_color_hex(0x51f051)
#define COLOR_DIM lv_color_hex(0x808080)

// Tab order: a left/right swipe pages through them horizontally.
// The last one (TAB_DEBUG) exists only when diagnostics mode is on, so the actual
// count lives in tab_count rather than in a define.
#define TAB_CURRENT 0
#define TAB_LIMIT 1
#define TAB_DASH 2
#define TAB_TRIP 3
#define TAB_LOCK 4
#define TAB_DEBUG 5
#define TAB_MAX 6

static uint8_t tab_count = 5;

// Lock code. The keypad only has 1..9, so the code cannot contain a zero.
#define LOCK_CODE "1987"
#define LOCK_CODE_LEN 4

#define CURRENT_STEP_A 1.0f   // step of the +/- buttons
#define CURRENT_MAX_A 60.0f   // setCurrent ceiling, A (cross-check with the VESC settings!)

static void ui_lock_reset_entry(void);

// Dots indicating the current tab (the tab buttons are hidden)
static void ui_update_dots(uint16_t act) {
  for (int i = 0; i < tab_count; i++) {
    if (!dots[i]) continue;
    lv_obj_set_style_bg_color(dots[i], (i == (int)act) ? COLOR_ACCENT : lv_color_hex(0x3a3a3a), 0);
  }
}

static void ui_tab_changed_cb(lv_event_t *e) {
  LV_UNUSED(e);
  if (!tabview) return;
  const uint16_t act = lv_tabview_get_tab_act(tabview);
  ui_update_dots(act);
  // Leaving the keypad clears a partially entered code: coming back starts from scratch
  if (act != TAB_LOCK) ui_lock_reset_entry();
}

// Brings the labels/colours in line with the current limit state
static void ui_refresh_limit(void) {
  if (lbl_limit_state) {
    lv_label_set_text(lbl_limit_state, limit25_on ? "ON" : "OFF");
    lv_obj_set_style_text_color(lbl_limit_state, limit25_on ? COLOR_ACCENT : lv_color_hex(0xbbbbbb), 0);
  }
  if (lbl_limit_badge) {
    if (limit25_on) lv_obj_clear_flag(lbl_limit_badge, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(lbl_limit_badge, LV_OBJ_FLAG_HIDDEN);
  }
}

static void ui_limit_btn_cb(lv_event_t *e) {
  LV_UNUSED(e);
  limit25_on = btn_limit && lv_obj_has_state(btn_limit, LV_STATE_CHECKED);
  ui_refresh_limit();
}

// ====== Lock: 1..9 keypad and the code ======
// What has been typed is kept as a string so comparing with LOCK_CODE is trivial.
static char lock_entry[LOCK_CODE_LEN + 1] = "";
static uint8_t lock_entry_len = 0;

static void ui_lock_update_mask(void) {
  if (!lbl_lock_mask) return;
  // "* * - -": how many digits have been entered
  char buf[LOCK_CODE_LEN * 2];
  int n = 0;
  for (int i = 0; i < LOCK_CODE_LEN; i++) {
    buf[n++] = (i < lock_entry_len) ? '*' : '-';
    if (i < LOCK_CODE_LEN - 1) buf[n++] = ' ';
  }
  buf[n] = '\0';
  lv_label_set_text(lbl_lock_mask, buf);
}

static void ui_lock_reset_entry(void) {
  lock_entry_len = 0;
  lock_entry[0] = '\0';
  ui_lock_update_mask();
}

static void ui_lock_set_status(const char *txt, lv_color_t color) {
  if (!lbl_lock_status) return;
  lv_label_set_text(lbl_lock_status, txt);
  lv_obj_set_style_text_color(lbl_lock_status, color, 0);
}

// Badge on the speedometer plus the hint on the lock tab
static void ui_refresh_lock(void) {
  if (lbl_lock_badge) {
    if (lock_on) lv_obj_clear_flag(lbl_lock_badge, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(lbl_lock_badge, LV_OBJ_FLAG_HIDDEN);
  }
  ui_lock_set_status(lock_on ? "LOCKED - code to unlock" : "enter code to lock",
                     lock_on ? lv_palette_main(LV_PALETTE_RED) : COLOR_DIM);
}

static void ui_lock_key_cb(lv_event_t *e) {
  lv_obj_t *bm = lv_event_get_target(e);
  const char *txt = lv_btnmatrix_get_btn_text(bm, lv_btnmatrix_get_selected_btn(bm));
  if (!txt || txt[0] == '\0') return;

  if (lock_entry_len < LOCK_CODE_LEN) {
    lock_entry[lock_entry_len++] = txt[0];
    lock_entry[lock_entry_len] = '\0';
  }
  ui_lock_update_mask();

  if (lock_entry_len < LOCK_CODE_LEN) {
    ui_lock_set_status("...", COLOR_DIM);
    return;
  }

  // The full code is entered: the same code both locks and unlocks
  const bool ok = strcmp(lock_entry, LOCK_CODE) == 0;
  ui_lock_reset_entry();
  if (ok) {
    lock_on = !lock_on;
    ui_refresh_lock();  // writes the current status and shows/hides the badge itself
  } else {
    ui_lock_set_status("WRONG CODE", lv_palette_main(LV_PALETTE_RED));
  }
}

// ====== Settings menu (the gear button on the speedometer) ======
static void ui_refresh_debug_btn(void) {
  if (btn_debug) {
    if (debug_enabled) lv_obj_add_state(btn_debug, LV_STATE_CHECKED);
    else lv_obj_clear_state(btn_debug, LV_STATE_CHECKED);
  }
  if (lbl_debug_state) {
    lv_label_set_text(lbl_debug_state, debug_enabled ? "DEBUG MODE: ON" : "DEBUG MODE: OFF");
    lv_obj_set_style_text_color(lbl_debug_state, debug_enabled ? COLOR_ACCENT : lv_color_white(), 0);
  }
}

// The tab set is fixed at build time (LVGL 8.3 has no lv_tabview_remove_tab), so the
// diagnostics toggle rebuilds the whole UI. Only via lv_async_call: the tree cannot be
// deleted from the callback of a button that lives inside it.
static void ui_rebuild_async_cb(void *unused) {
  LV_UNUSED(unused);
  lv_obj_clean(lv_layer_top());  // the settings overlay lives there
  lv_obj_clean(lv_scr_act());
  ui_build();
}

static void ui_debug_btn_cb(lv_event_t *e) {
  LV_UNUSED(e);
  debug_enabled = btn_debug && lv_obj_has_state(btn_debug, LV_STATE_CHECKED);
  ui_refresh_debug_btn();
  lv_async_call(ui_rebuild_async_cb, NULL);
}

static void ui_settings_open_cb(lv_event_t *e) {
  LV_UNUSED(e);
  if (settings_modal) lv_obj_clear_flag(settings_modal, LV_OBJ_FLAG_HIDDEN);
}

static void ui_settings_close_cb(lv_event_t *e) {
  LV_UNUSED(e);
  if (settings_modal) lv_obj_add_flag(settings_modal, LV_OBJ_FLAG_HIDDEN);
}

// A double tap on the speedometer is a quick limit toggle, so there is no need to
// swipe to tab 1 while riding. LVGL has no double click, so two CLICKED events in a
// row are caught instead.
#define DOUBLE_TAP_MS 400
static void ui_meter_click_cb(lv_event_t *e) {
  LV_UNUSED(e);
  static uint32_t last_click = 0;
  uint32_t now = lv_tick_get();
  // last_click == 0 means the very first tap; lv_tick_elaps() accounts for the tick
  // counter wrapping around.
  if (last_click != 0 && lv_tick_elaps(last_click) < DOUBLE_TAP_MS) {
    ui_set_limit25(!limit25_on);  // updates the button on tab 1 and the badge itself
    last_click = 0;               // a third tap does not count as a new pair
    return;
  }
  last_click = now;
}

// Current labels: a large "what is set" plus "what actually went to the VESC"
static void ui_refresh_current(void) {
  char buf[32];
  if (lbl_current_val) {
    lv_snprintf(buf, sizeof(buf), "%d.%d A", (int)current_setpoint,
                (int)((current_setpoint - (int)current_setpoint) * 10.0f + 0.05f));
    lv_label_set_text(lbl_current_val, buf);
  }
  if (lbl_current_applied) {
    lv_snprintf(buf, sizeof(buf), "applied: %d.%d A", (int)current_applied,
                (int)((current_applied - (int)current_applied) * 10.0f + 0.05f));
    lv_label_set_text(lbl_current_applied, buf);
    lv_obj_set_style_text_color(lbl_current_applied,
                               (current_applied > 0.01f) ? COLOR_ACCENT : COLOR_DIM, 0);
  }
  // Button: while exactly what is set has been sent to the VESC, the next press turns
  // the current off - and it is labelled accordingly (OFF, red). If the setpoint has
  // been changed since, the button becomes UPDATE again and applies the new value.
  const bool armed = current_applied > 0.01f &&
                     fabsf(current_applied - current_setpoint) < 0.005f;
  if (lbl_current_update) lv_label_set_text(lbl_current_update, armed ? "OFF" : "UPDATE");
  if (btn_current_update) {
    lv_obj_set_style_bg_color(btn_current_update,
                              armed ? lv_color_hex(0x8a1f1f) : lv_color_hex(0x1f7a1f), 0);
    lv_obj_set_style_bg_color(btn_current_update,
                              armed ? lv_palette_main(LV_PALETTE_RED) : COLOR_ACCENT,
                              LV_STATE_PRESSED);
  }
  if (lbl_current_badge) {
    if (current_applied > 0.01f) {
      lv_snprintf(buf, sizeof(buf), "I %d A", (int)(current_applied + 0.5f));
      lv_label_set_text(lbl_current_badge, buf);
      lv_obj_clear_flag(lbl_current_badge, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(lbl_current_badge, LV_OBJ_FLAG_HIDDEN);
    }
  }
}

// The "-" and "+" buttons: the step is passed through user_data. Long presses are
// handled too, so there is no need to tap 60 times.
static void ui_current_step_cb(lv_event_t *e) {
  const float step = (float)(intptr_t)lv_event_get_user_data(e) * CURRENT_STEP_A;
  float v = current_setpoint + step;
  if (v < 0.0f) v = 0.0f;
  if (v > CURRENT_MAX_A) v = CURRENT_MAX_A;
  current_setpoint = v;
  ui_refresh_current();
}

// UPDATE: only here does the setpoint become what loop() sends to the VESC.
// A second press (when applied already equals the setpoint) turns the current off:
// applied = 0, loop() stops sending setCurrent and after the VESC timeout (~1 s) the
// throttle is back in control.
static void ui_current_update_cb(lv_event_t *e) {
  LV_UNUSED(e);
  const bool armed = current_applied > 0.01f &&
                     fabsf(current_applied - current_setpoint) < 0.005f;
  current_applied = armed ? 0.0f : current_setpoint;
  ui_refresh_current();
}

// One decimal place with a minus sign (regeneration): the integer/fractional parts are
// computed from the magnitude, otherwise values in (-1..0) would lose the "-".
static void ui_format_amps(char *buf, size_t n, float a) {
  float mag = fabsf(a);
  int whole = (int)mag;
  int frac = (int)((mag - whole) * 10.0f);
  snprintf(buf, n, "%s%d.%d A", (a < -0.05f) ? "-" : "", whole, frac);
}

// One label per value: during a measurement it holds the maximum (in green), and in
// normal mode the smoothed current value (in white).
static void ui_refresh_currents(void) {
  char buf[24];
  const bool peak = peak_measuring;
  lv_color_t color = peak ? COLOR_ACCENT : lv_color_white();

  if (lbl_cur_motor) {
    ui_format_amps(buf, sizeof(buf), peak ? peak_motor_a : cur_motor_ema);
    lv_label_set_text(lbl_cur_motor, buf);
    lv_obj_set_style_text_color(lbl_cur_motor, color, 0);
  }
  if (lbl_cur_input) {
    ui_format_amps(buf, sizeof(buf), peak ? peak_input_a : cur_input_ema);
    lv_label_set_text(lbl_cur_input, buf);
    lv_obj_set_style_text_color(lbl_cur_input, color, 0);
  }
  if (lbl_peak_btn) {
    lv_label_set_text(lbl_peak_btn, peak ? "MEASURING (MAX)" : "MEASURE PEAK");
  }
}

// The kW label under the speedometer: during a measurement the maximum (in green),
// otherwise the smoothed current power (in the ring's colour). The ring itself is
// always live - it is an indicator rather than a reading, and it shows what is
// happening right now.
static void ui_refresh_power(void) {
  if (!lbl_power) return;
  const bool peak = peak_measuring;
  // Integer/fractional parts by hand: %f in lv_snprintf may be disabled in lv_conf.h
  float kw = (peak ? peak_power_w : power_ema) / 1000.0f;
  bool neg = kw < 0.0f;
  if (neg) kw = -kw;
  int whole = (int)kw;
  int frac = (int)((kw - whole) * 100.0f + 0.5f);
  if (frac >= 100) { frac -= 100; whole += 1; }
  char buf[24];
  lv_snprintf(buf, sizeof(buf), "%s%d.%02d kW", neg ? "-" : "", whole, frac);
  lv_label_set_text(lbl_power, buf);
  // Exceeding the power limit matters more than "measuring mode", so red beats green
  const float shown_w = peak ? peak_power_w : power_ema;
  lv_color_t color = (shown_w > POWER_ALERT_W) ? lv_palette_main(LV_PALETTE_RED)
                                               : (peak ? COLOR_ACCENT : COLOR_POWER);
  lv_obj_set_style_text_color(lbl_power, color, 0);
}

static void ui_peak_btn_cb(lv_event_t *e) {
  LV_UNUSED(e);
  bool on = btn_peak && lv_obj_has_state(btn_peak, LV_STATE_CHECKED);
  // Turning it on always starts a fresh measurement; turning it off leaves the numbers.
  if (on && !peak_measuring) {
    peak_motor_a = 0.0f;
    peak_input_a = 0.0f;
    peak_power_w = 0.0f;
  }
  peak_measuring = on;
  ui_refresh_currents();
  ui_refresh_power();
}

// Shared tab page style: black background, vertical flex, no scrolling (only the
// tabview's own container needs to scroll - that is what the swipe pages).
static void ui_style_page(lv_obj_t *page, lv_coord_t pad, lv_coord_t row_gap,
                          lv_flex_align_t main_align) {
  lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(page, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(page, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(page, 0, 0);
  lv_obj_set_style_pad_all(page, pad, 0);
  lv_obj_set_style_pad_row(page, row_gap, 0);
  lv_obj_set_layout(page, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(page, main_align, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
}

// Report row: a grey caption on top with a large value below it.
// Returns the value label.
static lv_obj_t *ui_report_row(lv_obj_t *parent, const char *caption, const char *initial) {
  lv_obj_t *box = lv_obj_create(parent);
  lv_obj_remove_style_all(box);
  lv_obj_set_size(box, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_layout(box, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *cap = lv_label_create(box);
  lv_label_set_text(cap, caption);
  lv_obj_set_style_text_color(cap, COLOR_DIM, 0);
  lv_obj_set_style_text_font(cap, &lv_font_montserrat_14, 0);

  lv_obj_t *val = lv_label_create(box);
  lv_label_set_text(val, initial);
  lv_obj_set_style_text_color(val, lv_color_white(), 0);
  lv_obj_set_style_text_font(val, &lv_font_montserrat_26, 0);
  return val;
}

// Two compact readings in a single row - saves height on the report tab, where no
// more full-size ui_report_row() rows would fit.
static void ui_report_pair(lv_obj_t *parent, const char *cap_a, const char *cap_b,
                           lv_obj_t **out_a, lv_obj_t **out_b) {
  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_layout(row, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  const char *caps[2] = { cap_a, cap_b };
  lv_obj_t **outs[2] = { out_a, out_b };
  for (int i = 0; i < 2; i++) {
    lv_obj_t *cell = lv_obj_create(row);
    lv_obj_remove_style_all(cell);
    lv_obj_set_size(cell, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_layout(cell, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cell, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *cap = lv_label_create(cell);
    lv_label_set_text(cap, caps[i]);
    lv_obj_set_style_text_color(cap, COLOR_DIM, 0);
    lv_obj_set_style_text_font(cap, &lv_font_montserrat_12, 0);

    lv_obj_t *val = lv_label_create(cell);
    lv_label_set_text(val, "0.0 A");
    lv_obj_set_style_text_color(val, lv_color_white(), 0);
    lv_obj_set_style_text_font(val, &lv_font_montserrat_18, 0);
    *outs[i] = val;
  }
}

// All widget pointers are statics, and ui_build() can be called again (the rebuild on
// the diagnostics toggle). They are cleared before building: some widgets (the
// diagnostics tab) do not always exist, and after lv_obj_clean() the old pointers would
// dangle on deleted objects.
static void ui_reset_refs(void) {
  lbl_batt = lbl_volt = lbl_speed = NULL;
  meter_speed = NULL;
  needle_speed = NULL;
  arc_speed = NULL;
  arc_power = lbl_power = NULL;
  lbl_temp_val = lbl_tachometer = lbl_tachometerAbs = lbl_cost = NULL;
  lbl_cur_motor = lbl_cur_input = NULL;
  btn_peak = lbl_peak_btn = lbl_debbug = NULL;
  tabview = NULL;
  btn_limit = lbl_limit_state = lbl_limit_badge = NULL;
  lbl_current_val = lbl_current_applied = lbl_current_badge = NULL;
  btn_current_update = lbl_current_update = NULL;
  lbl_lock_badge = lbl_lock_mask = lbl_lock_status = NULL;
  settings_modal = btn_debug = lbl_debug_state = NULL;
  lbl_diag_fault = lbl_diag_last = lbl_diag_duty = lbl_diag_tmotor = lbl_diag_link = NULL;
  for (int i = 0; i < TAB_MAX; i++) dots[i] = NULL;
}

void ui_build(void) {
  ui_reset_refs();

  // Tabs are paged by swiping (the tab buttons are hidden, height 0):
  // 0 = motor current (setCurrent), 1 = 25 km/h limit, 2 = speedometer (default),
  // 3 = trip report, 4 = lock, 5 = diagnostics (only when debug_enabled).
  tab_count = debug_enabled ? TAB_MAX : TAB_MAX - 1;
  lv_obj_set_style_bg_color(lv_scr_act(), lv_color_black(), 0);
  tabview = lv_tabview_create(lv_scr_act(), LV_DIR_TOP, 0);
  lv_obj_set_style_bg_color(tabview, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(tabview, LV_OPA_COVER, 0);
  lv_obj_add_flag(lv_tabview_get_tab_btns(tabview), LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_event_cb(tabview, ui_tab_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

  lv_obj_t *tab_current = lv_tabview_add_tab(tabview, "Current");
  lv_obj_t *tab_limit = lv_tabview_add_tab(tabview, "Limit");
  lv_obj_t *tab_dash = lv_tabview_add_tab(tabview, "Dash");
  lv_obj_t *tab_trip = lv_tabview_add_tab(tabview, "Trip");
  lv_obj_t *tab_lock = lv_tabview_add_tab(tabview, "Lock");
  lv_obj_t *tab_diag = debug_enabled ? lv_tabview_add_tab(tabview, "Diag") : NULL;
  ui_style_page(tab_current, 8, 8, LV_FLEX_ALIGN_CENTER);
  ui_style_page(tab_limit, 8, 10, LV_FLEX_ALIGN_CENTER);
  ui_style_page(tab_dash, 2, 4, LV_FLEX_ALIGN_CENTER);
  ui_style_page(tab_trip, 6, 0, LV_FLEX_ALIGN_SPACE_EVENLY);
  ui_style_page(tab_lock, 6, 6, LV_FLEX_ALIGN_CENTER);
  if (tab_diag) ui_style_page(tab_diag, 6, 0, LV_FLEX_ALIGN_SPACE_EVENLY);

  // ====== Leftmost tab: motor current (setCurrent) ======
  lv_obj_t *lbl_cur_title = lv_label_create(tab_current);
  lv_label_set_text(lbl_cur_title, "MOTOR CURRENT");
  lv_obj_set_style_text_color(lbl_cur_title, COLOR_DIM, 0);
  lv_obj_set_style_text_font(lbl_cur_title, &lv_font_montserrat_18, 0);

  lbl_current_val = lv_label_create(tab_current);
  lv_obj_set_style_text_color(lbl_current_val, lv_color_white(), 0);
  lv_obj_set_style_text_font(lbl_current_val, &lv_font_montserrat_48, 0);

  // Row with the "-" / "+" buttons
  lv_obj_t *cur_row = lv_obj_create(tab_current);
  lv_obj_remove_style_all(cur_row);
  lv_obj_set_size(cur_row, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_layout(cur_row, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(cur_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(cur_row, 10, 0);
  lv_obj_set_flex_align(cur_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  const char *step_txt[2] = { "-", "+" };
  const int step_dir[2] = { -1, +1 };
  for (int i = 0; i < 2; i++) {
    lv_obj_t *btn = lv_btn_create(cur_row);
    lv_obj_set_size(btn, 100, 62);
    lv_obj_set_style_radius(btn, 12, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x2b2b2b), 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x444444), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(btn, 2, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x555555), 0);
    // CLICKED + LONG_PRESSED_REPEAT: holding the button ramps the value quickly
    lv_obj_add_event_cb(btn, ui_current_step_cb, LV_EVENT_CLICKED, (void *)(intptr_t)step_dir[i]);
    lv_obj_add_event_cb(btn, ui_current_step_cb, LV_EVENT_LONG_PRESSED_REPEAT,
                        (void *)(intptr_t)step_dir[i]);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, step_txt[i]);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_38, 0);
    lv_obj_center(lbl);
  }

  // UPDATE sends the configured value to the VESC; a second press turns the current
  // off (the button's colour and label are driven by ui_refresh_current())
  btn_current_update = lv_btn_create(tab_current);
  lv_obj_set_size(btn_current_update, 210, 52);
  lv_obj_set_style_radius(btn_current_update, 12, 0);
  lv_obj_add_event_cb(btn_current_update, ui_current_update_cb, LV_EVENT_CLICKED, NULL);
  lbl_current_update = lv_label_create(btn_current_update);
  lv_obj_set_style_text_color(lbl_current_update, lv_color_white(), 0);
  lv_obj_set_style_text_font(lbl_current_update, &lv_font_montserrat_22, 0);
  lv_obj_center(lbl_current_update);

  lbl_current_applied = lv_label_create(tab_current);
  lv_obj_set_style_text_font(lbl_current_applied, &lv_font_montserrat_14, 0);

  ui_refresh_current();

  // ====== Tab to the left: 25 km/h speed limit ======
  lv_obj_t *lbl_limit_title = lv_label_create(tab_limit);
  lv_label_set_text(lbl_limit_title, "SPEED LIMIT");
  lv_obj_set_style_text_color(lbl_limit_title, COLOR_DIM, 0);
  lv_obj_set_style_text_font(lbl_limit_title, &lv_font_montserrat_18, 0);

  btn_limit = lv_btn_create(tab_limit);
  lv_obj_set_size(btn_limit, 180, 120);
  lv_obj_add_flag(btn_limit, LV_OBJ_FLAG_CHECKABLE);
  lv_obj_set_style_radius(btn_limit, 14, 0);
  lv_obj_set_style_bg_color(btn_limit, lv_color_hex(0x2b2b2b), 0);                    // OFF
  lv_obj_set_style_bg_color(btn_limit, lv_color_hex(0x1f7a1f), LV_STATE_CHECKED);     // ON
  lv_obj_set_style_border_width(btn_limit, 2, 0);
  lv_obj_set_style_border_color(btn_limit, lv_color_hex(0x555555), 0);
  lv_obj_set_style_border_color(btn_limit, COLOR_ACCENT, LV_STATE_CHECKED);
  lv_obj_add_event_cb(btn_limit, ui_limit_btn_cb, LV_EVENT_VALUE_CHANGED, NULL);
  // Default state is off (LV_STATE_CHECKED is not set)

  lv_obj_t *lbl_limit_val = lv_label_create(btn_limit);
  lv_label_set_text(lbl_limit_val, "25 km/h");
  lv_obj_set_style_text_color(lbl_limit_val, lv_color_white(), 0);
  lv_obj_set_style_text_font(lbl_limit_val, &lv_font_montserrat_26, 0);
  lv_obj_align(lbl_limit_val, LV_ALIGN_CENTER, 0, -22);

  lbl_limit_state = lv_label_create(btn_limit);
  lv_obj_set_style_text_font(lbl_limit_state, &lv_font_montserrat_38, 0);
  lv_obj_align(lbl_limit_state, LV_ALIGN_CENTER, 0, 22);

  lv_obj_t *lbl_limit_hint = lv_label_create(tab_limit);
  lv_label_set_text(lbl_limit_hint, "tap to toggle");
  lv_obj_set_style_text_color(lbl_limit_hint, COLOR_DIM, 0);
  lv_obj_set_style_text_font(lbl_limit_hint, &lv_font_montserrat_14, 0);

  // Not just a refresh: on a UI rebuild (the diagnostics toggle) the button is created
  // anew and its LV_STATE_CHECKED has to be restored from the state, not the other way.
  ui_set_limit25(limit25_on);

  // ====== Tab to the right: trip and cost report (vertical) ======
  lbl_tachometer = ui_report_row(tab_trip, "TRIP (SINCE BOOT)", "0.0 km");
  lbl_tachometerAbs = ui_report_row(tab_trip, "TOTAL", "0 km");
  lbl_cost = ui_report_row(tab_trip, "COST, ILS", "0.0");
  // Currents at the bottom: MOTOR is the phase current (torque), BATTERY the input
  // current (power). During a measurement these same labels show the maximum (green).
  ui_report_pair(tab_trip, "MOTOR", "BATTERY", &lbl_cur_motor, &lbl_cur_input);

  // Peak measurement button. The currents change faster than they can be read off the
  // screen, so the maximum is accumulated in the background instead of being eyeballed.
  btn_peak = lv_btn_create(tab_trip);
  lv_obj_set_size(btn_peak, 200, 38);
  lv_obj_add_flag(btn_peak, LV_OBJ_FLAG_CHECKABLE);
  lv_obj_set_style_radius(btn_peak, 10, 0);
  lv_obj_set_style_bg_color(btn_peak, lv_color_hex(0x2b2b2b), 0);                 // off
  lv_obj_set_style_bg_color(btn_peak, lv_color_hex(0x1f7a1f), LV_STATE_CHECKED);  // measuring
  lv_obj_set_style_border_width(btn_peak, 2, 0);
  lv_obj_set_style_border_color(btn_peak, lv_color_hex(0x555555), 0);
  lv_obj_set_style_border_color(btn_peak, COLOR_ACCENT, LV_STATE_CHECKED);
  lv_obj_add_event_cb(btn_peak, ui_peak_btn_cb, LV_EVENT_VALUE_CHANGED, NULL);
  // A UI rebuild must not interrupt an ongoing measurement - restore the button state
  if (peak_measuring) lv_obj_add_state(btn_peak, LV_STATE_CHECKED);

  // The settings gear sits in the free top corner of the report tab. IGNORE_LAYOUT so
  // it stays out of the page's flex column (the centred labels do not touch it).
  lv_obj_t *btn_settings = lv_btn_create(tab_trip);
  lv_obj_add_flag(btn_settings, LV_OBJ_FLAG_IGNORE_LAYOUT);
  lv_obj_set_size(btn_settings, 34, 34);
  lv_obj_set_style_radius(btn_settings, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(btn_settings, lv_color_hex(0x2b2b2b), 0);
  lv_obj_set_style_bg_color(btn_settings, lv_color_hex(0x444444), LV_STATE_PRESSED);
  lv_obj_set_style_border_width(btn_settings, 0, 0);
  lv_obj_align(btn_settings, LV_ALIGN_TOP_RIGHT, 0, 0);
  lv_obj_add_event_cb(btn_settings, ui_settings_open_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t *lbl_gear = lv_label_create(btn_settings);
  lv_label_set_text(lbl_gear, LV_SYMBOL_SETTINGS);
  lv_obj_set_style_text_color(lbl_gear, lv_color_hex(0xcccccc), 0);
  lv_obj_set_style_text_font(lbl_gear, &lv_font_montserrat_18, 0);
  lv_obj_center(lbl_gear);

  lbl_peak_btn = lv_label_create(btn_peak);
  lv_obj_set_style_text_color(lbl_peak_btn, lv_color_white(), 0);
  lv_obj_set_style_text_font(lbl_peak_btn, &lv_font_montserrat_14, 0);
  lv_obj_center(lbl_peak_btn);

  ui_refresh_currents();
  if (DEBUG_MODE == 1) {
    lbl_debbug = lv_label_create(tab_trip);
    lv_label_set_text(lbl_debbug, "Debug mode:");
    lv_obj_set_style_text_color(lbl_debbug, lv_color_make(255, 0, 0), 0);
    lv_obj_set_style_text_font(lbl_debbug, &lv_font_montserrat_14, 0);
  }

  // ====== Centre tab: speedometer ======
  lv_obj_t *root = tab_dash;

  // ====== Block 1: Speed (speedometer) ======
  // Speedometer: a circular scale with a needle and a coloured arc
  meter_speed = lv_meter_create(root);
  lv_obj_clear_flag(meter_speed, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(meter_speed, METER_SIZE, METER_SIZE);
  lv_obj_set_style_bg_opa(meter_speed, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(meter_speed, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(meter_speed, METER_PAD, LV_PART_MAIN);
  lv_obj_set_style_text_font(meter_speed, &lv_font_montserrat_12, LV_PART_TICKS);
  // A double tap on the dial toggles the 25 km/h limit. Swiping between tabs still
  // works: the speedometer is not scrollable, so the gesture goes to the tabview, and
  // LVGL does not emit CLICKED after a drag.
  lv_obj_add_flag(meter_speed, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(meter_speed, ui_meter_click_cb, LV_EVENT_CLICKED, NULL);

  lv_meter_scale_t *scale = lv_meter_add_scale(meter_speed);
  // 56 minor ticks (0..55), every 5th is a major tick with a label
  lv_meter_set_scale_ticks(meter_speed, scale, 56, 2, 6, lv_color_hex(0x808080));
  lv_meter_set_scale_major_ticks(meter_speed, scale, 5, 3, 11, lv_color_white(), 12);
  // range 0..40 km/h, a 270 degree arc starting at 135 degrees (bottom left)
  lv_meter_set_scale_range(meter_speed, scale, 0, SPEED_MAX_KMH, 270, 135);

  // Coloured arc that grows with the speed
  arc_speed = lv_meter_add_arc(meter_speed, scale, 5, lv_color_hex(0x51f051), 0);
  lv_meter_set_indicator_start_value(meter_speed, arc_speed, 0);
  lv_meter_set_indicator_end_value(meter_speed, arc_speed, 0);

  // Needle
  needle_speed = lv_meter_add_needle_line(meter_speed, scale, 4, lv_color_hex(0x51f051), -8);
  lv_meter_set_indicator_value(meter_speed, needle_speed, 0);

  // Wheel power ring: a blue line along the outer edge of the speedometer.
  // The geometry mirrors the scale (start at 135 degrees, 270 degrees clockwise) so the
  // ring reads as a continuation of the dial. It is a child of the speedometer, so the
  // centres line up automatically without manual alignment.
  arc_power = lv_arc_create(meter_speed);
  lv_obj_remove_style(arc_power, NULL, LV_PART_KNOB);  // do not show the knob
  lv_obj_set_size(arc_power, METER_SIZE - 2, METER_SIZE - 2);
  lv_obj_center(arc_power);
  // Do not intercept touches: the double tap on the dial and the tab swipe must reach
  // the speedometer/tabview underneath the ring.
  lv_obj_clear_flag(arc_power, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(arc_power, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(arc_power, 0, LV_PART_MAIN);
  lv_obj_set_style_arc_width(arc_power, POWER_ARC_W, LV_PART_MAIN);
  lv_obj_set_style_arc_color(arc_power, lv_color_hex(0x1b2a3a), LV_PART_MAIN);
  lv_obj_set_style_arc_width(arc_power, POWER_ARC_W, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(arc_power, COLOR_POWER, LV_PART_INDICATOR);
  lv_arc_set_bg_angles(arc_power, 135, 45);
  lv_arc_set_range(arc_power, 0, POWER_MAX_W);
  lv_arc_set_value(arc_power, 0);

  // Speed number in the centre of the speedometer
  lbl_speed = lv_label_create(meter_speed);
  lv_label_set_text(lbl_speed, "--");
  lv_obj_set_style_text_color(lbl_speed, lv_color_white(), 0);
  lv_obj_set_style_text_font(lbl_speed, &lv_font_montserrat_48, 0);
  lv_obj_set_style_text_align(lbl_speed, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(lbl_speed, LV_ALIGN_CENTER, 0, -10);

  // The "km/h" caption below the number
  lv_obj_t *lbl_speed_unit = lv_label_create(meter_speed);
  lv_label_set_text(lbl_speed_unit, "km/h");
  lv_obj_set_style_text_color(lbl_speed_unit, lv_color_hex(0xaaaaaa), 0);
  lv_obj_set_style_text_font(lbl_speed_unit, &lv_font_montserrat_14, 0);
  lv_obj_align(lbl_speed_unit, LV_ALIGN_CENTER, 0, 28);

  // Power in kW - in the dial's bottom gap (no scale there), in the ring's colour
  lbl_power = lv_label_create(meter_speed);
  lv_label_set_text(lbl_power, "0.00 kW");
  lv_obj_set_style_text_color(lbl_power, COLOR_POWER, 0);
  lv_obj_set_style_text_font(lbl_power, &lv_font_montserrat_18, 0);
  // +58: below the labels of the outermost ticks (0 and 55) so it does not overlap them
  lv_obj_align(lbl_power, LV_ALIGN_CENTER, 0, 58);

  // ====== Block 2: the Battery % / Voltage / Temp row ======
  lv_obj_t *info = lv_obj_create(root);
  lv_obj_clear_flag(info, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(info, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(info, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(info, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_left(info, 4, LV_PART_MAIN);
  lv_obj_set_style_pad_right(info, 4, LV_PART_MAIN);
  lv_obj_set_size(info, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_layout(info, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(info, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(info,
                        LV_FLEX_ALIGN_SPACE_BETWEEN,  // along X
                        LV_FLEX_ALIGN_CENTER,         // along Y
                        LV_FLEX_ALIGN_CENTER);

  // Battery %
  lbl_batt = lv_label_create(info);
  lv_label_set_text(lbl_batt, "--%");
  lv_obj_set_style_text_color(lbl_batt, lv_color_white(), 0);
  lv_obj_set_style_text_font(lbl_batt, &lv_font_montserrat_22, 0);

  // Voltage
  lbl_volt = lv_label_create(info);
  lv_label_set_text(lbl_volt, "--.-V");
  lv_obj_set_style_text_color(lbl_volt, lv_color_white(), 0);
  lv_obj_set_style_text_font(lbl_volt, &lv_font_montserrat_22, 0);

  // Controller temperature (the "Temp" caption was dropped - not needed in 240 px)
  lbl_temp_val = lv_label_create(info);
  lv_label_set_text(lbl_temp_val, "--°C");
  lv_obj_set_style_text_color(lbl_temp_val, lv_color_white(), 0);
  lv_obj_set_style_text_font(lbl_temp_val, &lv_font_montserrat_22, 0);

  // Badges for the active modes - shown on the speedometer so the state does not have
  // to be looked up on the neighbouring tabs
  lv_obj_t *badges = lv_obj_create(root);
  lv_obj_remove_style_all(badges);
  lv_obj_set_size(badges, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_layout(badges, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(badges, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(badges, 12, 0);
  lv_obj_set_flex_align(badges, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lbl_limit_badge = lv_label_create(badges);
  lv_label_set_text(lbl_limit_badge, "LIMIT 25");
  lv_obj_set_style_text_color(lbl_limit_badge, COLOR_ACCENT, 0);
  lv_obj_set_style_text_font(lbl_limit_badge, &lv_font_montserrat_14, 0);
  lv_obj_add_flag(lbl_limit_badge, LV_OBJ_FLAG_HIDDEN);

  lbl_current_badge = lv_label_create(badges);
  lv_label_set_text(lbl_current_badge, "I 0 A");
  lv_obj_set_style_text_color(lbl_current_badge, COLOR_ACCENT, 0);
  lv_obj_set_style_text_font(lbl_current_badge, &lv_font_montserrat_14, 0);
  lv_obj_add_flag(lbl_current_badge, LV_OBJ_FLAG_HIDDEN);

  // Padlock: LVGL 8.3 has no built-in lock symbol, so a word is used instead
  lbl_lock_badge = lv_label_create(badges);
  lv_label_set_text(lbl_lock_badge, "LOCKED");
  lv_obj_set_style_text_color(lbl_lock_badge, lv_palette_main(LV_PALETTE_RED), 0);
  lv_obj_set_style_text_font(lbl_lock_badge, &lv_font_montserrat_14, 0);
  lv_obj_add_flag(lbl_lock_badge, LV_OBJ_FLAG_HIDDEN);

  // ====== Lock tab: 1..9 keypad ======
  // The title and the mask on top also leave a strip that is convenient to start a
  // swipe from - a full-page button grid would get in the way of paging tabs.
  lv_obj_t *lbl_lock_title = lv_label_create(tab_lock);
  lv_label_set_text(lbl_lock_title, "LOCK");
  lv_obj_set_style_text_color(lbl_lock_title, COLOR_DIM, 0);
  lv_obj_set_style_text_font(lbl_lock_title, &lv_font_montserrat_18, 0);

  lbl_lock_mask = lv_label_create(tab_lock);
  lv_obj_set_style_text_color(lbl_lock_mask, lv_color_white(), 0);
  lv_obj_set_style_text_font(lbl_lock_mask, &lv_font_montserrat_38, 0);

  static const char *lock_keys[] = { "1", "2", "3", "\n",
                                     "4", "5", "6", "\n",
                                     "7", "8", "9", "" };
  lv_obj_t *keypad = lv_btnmatrix_create(tab_lock);
  lv_btnmatrix_set_map(keypad, lock_keys);
  lv_obj_set_size(keypad, 216, 150);
  lv_obj_set_style_bg_opa(keypad, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(keypad, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(keypad, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_row(keypad, 6, LV_PART_MAIN);
  lv_obj_set_style_pad_column(keypad, 6, LV_PART_MAIN);
  lv_obj_set_style_bg_color(keypad, lv_color_hex(0x2b2b2b), LV_PART_ITEMS);
  lv_obj_set_style_bg_color(keypad, lv_color_hex(0x444444), LV_PART_ITEMS | LV_STATE_PRESSED);
  lv_obj_set_style_border_width(keypad, 2, LV_PART_ITEMS);
  lv_obj_set_style_border_color(keypad, lv_color_hex(0x555555), LV_PART_ITEMS);
  lv_obj_set_style_radius(keypad, 10, LV_PART_ITEMS);
  lv_obj_set_style_text_color(keypad, lv_color_white(), LV_PART_ITEMS);
  lv_obj_set_style_text_font(keypad, &lv_font_montserrat_26, LV_PART_ITEMS);
  lv_obj_add_event_cb(keypad, ui_lock_key_cb, LV_EVENT_VALUE_CHANGED, NULL);

  lbl_lock_status = lv_label_create(tab_lock);
  lv_obj_set_style_text_font(lbl_lock_status, &lv_font_montserrat_14, 0);

  ui_lock_reset_entry();
  ui_refresh_lock();

  // ====== Diagnostics tab (only when the mode is on) ======
  if (tab_diag) {
    lbl_diag_fault = ui_report_row(tab_diag, "VESC FAULT", "NONE");
    lbl_diag_last = ui_report_row(tab_diag, "LAST FAULT (SINCE BOOT)", "NONE");
    // Fault names are long ("GATE DRV UNDER VOLT"), so they are word-wrapped -
    // otherwise the line runs off the edges of the 240 px screen.
    lv_obj_t *const diag_wrap[2] = { lbl_diag_fault, lbl_diag_last };
    for (int i = 0; i < 2; i++) {
      lv_obj_set_width(diag_wrap[i], LV_PCT(100));
      lv_label_set_long_mode(diag_wrap[i], LV_LABEL_LONG_WRAP);
      lv_obj_set_style_text_align(diag_wrap[i], LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_set_style_text_font(diag_wrap[i], &lv_font_montserrat_18, 0);
    }
    ui_report_pair(tab_diag, "DUTY", "MOTOR T", &lbl_diag_duty, &lbl_diag_tmotor);
    lv_label_set_text(lbl_diag_duty, "--");
    lv_label_set_text(lbl_diag_tmotor, "--");
    lbl_diag_link = lv_label_create(tab_diag);
    lv_label_set_text(lbl_diag_link, "VESC LINK: ...");
    lv_obj_set_style_text_color(lbl_diag_link, COLOR_DIM, 0);
    lv_obj_set_style_text_font(lbl_diag_link, &lv_font_montserrat_14, 0);
  }

  // ====== Settings menu: an overlay above everything (including the tab dots) ======
  settings_modal = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(settings_modal);
  lv_obj_set_size(settings_modal, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(settings_modal, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(settings_modal, LV_OPA_70, 0);
  // Clickable backdrop: swallows presses on the UI beneath the menu
  lv_obj_add_flag(settings_modal, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(settings_modal, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(settings_modal, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t *panel = lv_obj_create(settings_modal);
  lv_obj_set_size(panel, 216, 180);
  lv_obj_center(panel);
  lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(panel, lv_color_hex(0x141414), 0);
  lv_obj_set_style_border_color(panel, lv_color_hex(0x555555), 0);
  lv_obj_set_style_border_width(panel, 2, 0);
  lv_obj_set_style_radius(panel, 14, 0);
  lv_obj_set_style_pad_all(panel, 10, 0);
  lv_obj_set_layout(panel, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(panel, 10, 0);
  lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *lbl_settings_title = lv_label_create(panel);
  lv_label_set_text(lbl_settings_title, "SETTINGS");
  lv_obj_set_style_text_color(lbl_settings_title, COLOR_DIM, 0);
  lv_obj_set_style_text_font(lbl_settings_title, &lv_font_montserrat_18, 0);

  // Diagnostics toggle: adds/removes the last tab (through a UI rebuild)
  btn_debug = lv_btn_create(panel);
  lv_obj_set_size(btn_debug, 180, 52);
  lv_obj_add_flag(btn_debug, LV_OBJ_FLAG_CHECKABLE);
  lv_obj_set_style_radius(btn_debug, 12, 0);
  lv_obj_set_style_bg_color(btn_debug, lv_color_hex(0x2b2b2b), 0);
  lv_obj_set_style_bg_color(btn_debug, lv_color_hex(0x1f7a1f), LV_STATE_CHECKED);
  lv_obj_set_style_border_width(btn_debug, 2, 0);
  lv_obj_set_style_border_color(btn_debug, lv_color_hex(0x555555), 0);
  lv_obj_set_style_border_color(btn_debug, COLOR_ACCENT, LV_STATE_CHECKED);
  lv_obj_add_event_cb(btn_debug, ui_debug_btn_cb, LV_EVENT_VALUE_CHANGED, NULL);

  lbl_debug_state = lv_label_create(btn_debug);
  lv_obj_set_style_text_font(lbl_debug_state, &lv_font_montserrat_14, 0);
  lv_obj_center(lbl_debug_state);

  lv_obj_t *btn_close = lv_btn_create(panel);
  lv_obj_set_size(btn_close, 180, 40);
  lv_obj_set_style_radius(btn_close, 12, 0);
  lv_obj_set_style_bg_color(btn_close, lv_color_hex(0x2b2b2b), 0);
  lv_obj_add_event_cb(btn_close, ui_settings_close_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *lbl_close = lv_label_create(btn_close);
  lv_label_set_text(lbl_close, "CLOSE");
  lv_obj_set_style_text_color(lbl_close, lv_color_white(), 0);
  lv_obj_set_style_text_font(lbl_close, &lv_font_montserrat_14, 0);
  lv_obj_center(lbl_close);

  ui_refresh_debug_btn();

  // ====== Current tab indicator (dots at the bottom) ======
  lv_obj_t *dot_box = lv_obj_create(lv_scr_act());
  lv_obj_remove_style_all(dot_box);
  lv_obj_set_size(dot_box, 15 * tab_count, 12);
  lv_obj_align(dot_box, LV_ALIGN_BOTTOM_MID, 0, -3);
  lv_obj_clear_flag(dot_box, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(dot_box, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_layout(dot_box, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(dot_box, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(dot_box, 8, 0);
  lv_obj_set_flex_align(dot_box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  for (int i = 0; i < tab_count; i++) {
    dots[i] = lv_obj_create(dot_box);
    lv_obj_remove_style_all(dots[i]);
    lv_obj_set_size(dots[i], 7, 7);
    lv_obj_set_style_radius(dots[i], LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(dots[i], LV_OPA_COVER, 0);
  }

  // Start on the speedometer: limit and current to the left, the report to the right
  lv_tabview_set_act(tabview, TAB_DASH, LV_ANIM_OFF);
  ui_update_dots(TAB_DASH);
}

// Helpers for updating the values:
void ui_set_battery(float volts) {
  uint8_t percent = calcBatteryPercent(volts);

  if (!lbl_batt || !lbl_volt) return;

  lv_color_t color;

  if (percent < 25) {
    color = lv_palette_main(LV_PALETTE_RED);
  } else if (percent < 40) {
    color = lv_palette_main(LV_PALETTE_YELLOW);
  } else if (percent < 80) {
    color = lv_palette_main(LV_PALETTE_GREEN);
  } else {
    color = lv_palette_main(LV_PALETTE_BLUE);
  }

  lv_obj_set_style_text_color(lbl_volt, color, 0);

  char buf1[32];
  lv_snprintf(buf1, sizeof(buf1), "%u%%", percent);
  lv_label_set_text(lbl_batt, buf1);

  char buf[16];
  int whole = (int)volts;
  int frac = (int)((volts - whole) * 10.0f);
  sprintf(buf, "%d.%dV", whole, abs(frac));
  lv_label_set_text(lbl_volt, buf);
}

// The current (animated) needle position in tenths of km/h.
// The speedometer scale is integer (0..SPEED_MAX_KMH), so the value is kept x10 to make
// the animation smooth and free of steps.
static int32_t needle_cur_x10 = 0;
// Smoothed speed (exponential moving average) - damps the VESC telemetry noise.
static float speed_ema = 0.0f;
static bool speed_ema_init = false;

// Animation callback: gets a value in tenths of km/h and moves the needle and the arc.
static void speed_anim_cb(void *var, int32_t x10) {
  needle_cur_x10 = x10;
  int32_t v = (x10 + 5) / 10;  // round to whole km/h for the scale
  if (meter_speed && needle_speed) {
    lv_meter_set_indicator_value(meter_speed, needle_speed, v);
  }
  if (meter_speed && arc_speed) {
    lv_meter_set_indicator_end_value(meter_speed, arc_speed, v);
  }
}

void ui_set_speed(float rpm) {
  if (!lbl_speed) return;
  float speed = erpm_to_kmh(rpm, POLE_PAIRS, WHEEL_CIRC_M);  // or "%.0f km/h"

  // Smooth the speed so the needle does not twitch on telemetry noise.
  if (!speed_ema_init) {
    speed_ema = speed;
    speed_ema_init = true;
  } else {
    speed_ema += 0.35f * (speed - speed_ema);  // coefficient 0.35: a response/smoothness compromise
  }
  speed = speed_ema;

  char buf[16];
  int whole = (int)speed;
  int frac = (int)((speed - whole) * 10.0f);
  sprintf(buf, "%d.%d", whole, abs(frac));
  lv_label_set_text(lbl_speed, buf);

  // Above the threshold the whole dial (number, needle, arc) is red, otherwise green/white.
  lv_color_t speed_color = (speed > SPEED_ALERT_KMH) ? lv_palette_main(LV_PALETTE_RED)
                                                      : lv_color_hex(0x51f051);
  lv_obj_set_style_text_color(lbl_speed, (speed > SPEED_ALERT_KMH) ? lv_palette_main(LV_PALETTE_RED)
                                                                   : lv_color_white(),
                              0);
  if (needle_speed) needle_speed->type_data.needle_line.color = speed_color;
  if (arc_speed) arc_speed->type_data.arc.color = speed_color;
  if (meter_speed) lv_obj_invalidate(meter_speed);

  // Target needle position in tenths of km/h, clamped to the scale's range.
  int32_t target_x10 = (int32_t)(speed * 10.0f + 0.5f);
  if (target_x10 < 0) target_x10 = 0;
  if (target_x10 > SPEED_MAX_KMH * 10) target_x10 = SPEED_MAX_KMH * 10;

  if (meter_speed && needle_speed) {
    // Ease the needle from its current position to the target.
    // Restarting with the same (var, exec_cb) replaces the previous animation, so the
    // needle always catches up with the latest value without jerking.
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, meter_speed);
    lv_anim_set_exec_cb(&a, speed_anim_cb);
    lv_anim_set_values(&a, needle_cur_x10, target_x10);
    lv_anim_set_time(&a, 300);  // slightly longer than the poll interval - continuous motion
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
  }
}

void ui_set_temp(float celsius) {
  if (!lbl_temp_val) return;

  if (celsius > 39) {
    lv_obj_set_style_text_color(lbl_temp_val, lv_palette_main(LV_PALETTE_RED), 0);
  }

  char buf[16];
  int whole = (int)celsius;                         // integer part
  int frac = (int)fabs((celsius - whole) * 10.0f);  // 1 digit after the point
  // the degree sign may be missing from the font, then just " C"
  sprintf(buf, "%d.%d°C", whole, frac);
  // sprintf(buf, "%d.%d C", whole, frac); // if the degree glyph is missing
  lv_label_set_text(lbl_temp_val, buf);
}

// Labels on the report tab - values only (the captions are static).
void ui_set_tachometer(float tachometer) {
  if (!lbl_tachometer) return;
  float km = ((tachometer / (POLE_PAIRS * 2 * 3)) * WHEEL_CIRC_M) / 1000.0;
  char buf[48];
  int whole = (int)km;                         // integer part
  int frac = (int)fabs((km - whole) * 10.0f);  // 1 digit after the point
  sprintf(buf, "%d.%d km", whole, frac);
  lv_label_set_text(lbl_tachometer, buf);
}

void ui_set_tachometerAbs(float tachometer) {
  if (!lbl_tachometerAbs) return;
  float km = ((tachometer / (POLE_PAIRS * 2 * 3)) * WHEEL_CIRC_M) / 1000.0;
  char buf[48];
  sprintf(buf, "%d km", (int)km);
  lv_label_set_text(lbl_tachometerAbs, buf);
}

void ui_set_cost(float ils) {
  if (!lbl_cost) return;
  char buf[32];
  int whole = (int)ils;
  int frac = (int)fabs((ils - whole) * 10.0f);
  // The shekel sign (U+20AA) is missing from Montserrat - printed without a currency symbol.
  sprintf(buf, "%d.%d", whole, frac);
  lv_label_set_text(lbl_cost, buf);
}

void ui_set_power(float watts) {
  if (!power_ema_init) {
    power_ema = watts;
    power_ema_init = true;
  } else {
    power_ema += 0.25f * (watts - power_ema);
  }

  // The peak uses the RAW value (smoothing cuts off exactly what is being measured),
  // the same principle as the peak currents in ui_set_currents().
  if (peak_measuring && watts > peak_power_w) peak_power_w = watts;

  if (arc_power) {
    // Regeneration (negative) is not shown by the ring, only by the label.
    int32_t v = (int32_t)(power_ema + (power_ema >= 0 ? 0.5f : -0.5f));
    if (v < 0) v = 0;
    if (v > POWER_MAX_W) v = POWER_MAX_W;
    lv_arc_set_value(arc_power, v);
    // The ring is always live (even during a measurement), so its colour follows the
    // current power: a full red ring means POWER_ALERT_W has been exceeded.
    lv_obj_set_style_arc_color(arc_power,
                               (power_ema > POWER_ALERT_W) ? lv_palette_main(LV_PALETTE_RED)
                                                           : COLOR_POWER,
                               LV_PART_INDICATOR);
  }

  ui_refresh_power();
}

void ui_set_currents(float motor_a, float input_a) {
  if (!lbl_cur_motor || !lbl_cur_input) return;

  if (!cur_ema_init) {
    cur_motor_ema = motor_a;
    cur_input_ema = input_a;
    cur_ema_init = true;
  } else {
    cur_motor_ema += 0.25f * (motor_a - cur_motor_ema);
    cur_input_ema += 0.25f * (input_a - cur_input_ema);
  }

  // Peaks are taken from the RAW values: smoothing cuts off exactly what we are trying
  // to measure. Negative values (regeneration) are ignored - what matters is the maximum
  // current delivered.
  if (peak_measuring) {
    if (motor_a > peak_motor_a) peak_motor_a = motor_a;
    if (input_a > peak_input_a) peak_input_a = input_a;
  }

  ui_refresh_currents();
}

// ===== 25 km/h speed limit =====
bool ui_get_limit25(void) {
  return limit25_on;
}

void ui_set_limit25(bool on) {
  limit25_on = on;
  if (btn_limit) {
    if (on) lv_obj_add_state(btn_limit, LV_STATE_CHECKED);
    else lv_obj_clear_state(btn_limit, LV_STATE_CHECKED);
  }
  ui_refresh_limit();
}

// ===== Lock =====
bool ui_get_lock(void) {
  return lock_on;
}

// Called at boot with the state restored from NVS
void ui_set_lock(bool on) {
  lock_on = on;
  ui_lock_reset_entry();
  ui_refresh_lock();
}

// ===== Diagnostics mode =====
bool ui_get_debug_enabled(void) {
  return debug_enabled;
}

void ui_set_debug_enabled(bool on) {
  debug_enabled = on;
  ui_refresh_debug_btn();  // the tab itself appears on the next UI build
}

// Names of the VESC fault codes. The order matches mc_fault_code from the VescUart
// library's datatypes.h (that header is not pulled in here: ui.cpp is also compiled by
// the PC simulator).
static const char *const FAULT_NAMES[] = {
  "NONE", "OVER VOLTAGE", "UNDER VOLTAGE", "DRV", "ABS OVER CURRENT",
  "OVER TEMP FET", "OVER TEMP MOTOR", "GATE DRV OVER VOLT", "GATE DRV UNDER VOLT",
  "MCU UNDER VOLTAGE", "WATCHDOG RESET", "ENCODER SPI", "ENC SINCOS LOW",
  "ENC SINCOS HIGH", "FLASH CORRUPTION", "OFFSET CURR SENS 1", "OFFSET CURR SENS 2",
  "OFFSET CURR SENS 3", "UNBALANCED CURRENTS", "BRK", "RESOLVER LOT",
  "RESOLVER DOS", "RESOLVER LOS", "FLASH CORRUPT APP", "FLASH CORRUPT MC",
  "ENCODER NO MAGNET", "ENC MAGNET STRONG", "PHASE FILTER",
};
#define FAULT_NAMES_N ((int)(sizeof(FAULT_NAMES) / sizeof(FAULT_NAMES[0])))

// The last non-zero fault since boot: the code in the telemetry clears itself once the
// cause is gone, so without a latch short faults are simply never seen.
static int last_fault_code = 0;

static void ui_fault_name(char *buf, size_t n, int code) {
  if (code >= 0 && code < FAULT_NAMES_N) lv_snprintf(buf, n, "%s", FAULT_NAMES[code]);
  else lv_snprintf(buf, n, "FAULT #%d", code);
}

void ui_set_diag(int fault_code, float duty, float temp_motor, bool comm_ok) {
  if (fault_code != 0) last_fault_code = fault_code;
  if (!lbl_diag_fault) return;  // the diagnostics tab was not built

  char buf[32];
  ui_fault_name(buf, sizeof(buf), fault_code);
  lv_label_set_text(lbl_diag_fault, buf);
  lv_obj_set_style_text_color(lbl_diag_fault,
                              fault_code ? lv_palette_main(LV_PALETTE_RED) : COLOR_ACCENT, 0);

  if (lbl_diag_last) {
    ui_fault_name(buf, sizeof(buf), last_fault_code);
    lv_label_set_text(lbl_diag_last, buf);
    lv_obj_set_style_text_color(lbl_diag_last,
                                last_fault_code ? lv_palette_main(LV_PALETTE_YELLOW)
                                                : lv_color_white(), 0);
  }
  if (lbl_diag_duty) {
    lv_snprintf(buf, sizeof(buf), "%d %%", (int)(duty * 100.0f + (duty >= 0 ? 0.5f : -0.5f)));
    lv_label_set_text(lbl_diag_duty, buf);
  }
  if (lbl_diag_tmotor) {
    int whole = (int)temp_motor;
    int frac = (int)fabsf((temp_motor - whole) * 10.0f);
    lv_snprintf(buf, sizeof(buf), "%d.%d C", whole, frac);
    lv_label_set_text(lbl_diag_tmotor, buf);
  }
  if (lbl_diag_link) {
    lv_label_set_text(lbl_diag_link, comm_ok ? "VESC LINK: OK" : "VESC LINK: NO DATA");
    lv_obj_set_style_text_color(lbl_diag_link,
                                comm_ok ? COLOR_ACCENT : lv_palette_main(LV_PALETTE_RED), 0);
  }
}

// ===== Motor current (setCurrent) =====
// The value confirmed with the UPDATE button. loop() must repeat it to the VESC: the
// controller times commands out after ~1 s, so a single send would simply decay.
float ui_get_current_applied(void) {
  return current_applied;
}

// The last displayed (smoothed) speed - the limit decision is based on it so the
// behaviour matches what the rider sees on the speedometer.
float ui_speed_kmh(void) {
  return speed_ema_init ? speed_ema : 0.0f;
}

// The inverse of erpm_to_kmh() using THE SAME constants as the speedometer.
float ui_kmh_to_erpm(float kmh) {
  return (kmh * 1000.0f / 60.0f / WHEEL_CIRC_M) * (float)POLE_PAIRS;
}
