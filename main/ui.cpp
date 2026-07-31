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

// Реализации в main/utils.cpp (Arduino-сборка) или simulator/arduino_shim.cpp (PC-симулятор).
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
static lv_obj_t *dots[6] = { NULL, NULL, NULL, NULL, NULL, NULL };  // размер = TAB_MAX

// Ограничение скорости 25 км/ч. Живёт только в RAM: при каждом включении
// контроллера должно быть ВЫКЛЮЧЕНО (в NVS специально не сохраняем).
// volatile: пишется из задачи LVGL (колбэк кнопки), читается из loop()
static volatile bool limit25_on = false;

// Замок: пока включён, loop() отбирает у газа тягу в самом VESC и держит тормоз.
// В отличие от лимита 25 состояние сохраняется в NVS (иначе замок бессмыслен) —
// пишет его loop(), здесь только флаг. volatile: пишется из задачи LVGL.
static volatile bool lock_on = false;
// Режим диагностики: добавляет последнюю вкладку. Тоже живёт в NVS.
static volatile bool debug_enabled = false;

// Ток мотора (setCurrent): setpoint крутится кнопками +/-, а applied
// применяется только по кнопке UPDATE — его и повторяет loop() в VESC.
static volatile float current_setpoint = 0.0f;
static volatile float current_applied = 0.0f;

// Замер пиковых токов (вкладка отчёта). Пока кнопка нажата, ui_set_currents()
// копит максимумы и показывает их ВМЕСТО живых значений — в тех же лейблах,
// отличие только по цвету. Включение кнопки начинает замер заново. В NVS не
// пишем — это разовое измерение, после перезагрузки не нужно.
static volatile bool peak_measuring = false;
static volatile float peak_motor_a = 0.0f;
static volatile float peak_input_a = 0.0f;
// Пиковая мощность копится тем же замером: кнопка на вкладке отчёта, а
// показывается на спидометре — в лейбле кВт вместо живого значения.
static volatile float peak_power_w = 0.0f;

// Сглаживание живых токов: VESC усредняет их лишь за период ШИМ, «сырые»
// значения на экране пляшут. Коэффициент мягче, чем у спидометра — цифры
// без стрелки. Пики при этом копятся по сырым (см. ui_set_currents).
static float cur_motor_ema = 0.0f;
static float cur_input_ema = 0.0f;
static bool cur_ema_init = false;

// Сглаживание мощности: она считается из входного тока, а он на экране пляшет
// сильнее всего (произведение двух шумных величин).
static float power_ema = 0.0f;
static bool power_ema_init = false;

extern const lv_font_t lv_font_montserrat_48;
extern const lv_font_t lv_font_montserrat_38;
extern const lv_font_t lv_font_montserrat_26;
extern const lv_font_t lv_font_montserrat_22;
extern const lv_font_t lv_font_montserrat_18;
extern const lv_font_t lv_font_montserrat_14;
extern const lv_font_t lv_font_montserrat_12;

#define POLE_PAIRS 15  // обычно 7 для 14-полюсного мотора
#define WHEEL_DIAMETER_M 0.255
#define WHEEL_CIRC_M (3.1415926f * WHEEL_DIAMETER_M)  // окружность в метрах
#define TACHO_COUNTS_PER_REV 8192.0f
#define SPEED_MAX_KMH 55       // верхний предел шкалы спидометра
#define SPEED_ALERT_KMH 50.0f  // выше этого порога спидометр краснеет
#define METER_SIZE 190         // диаметр спидометра, px (влезает в 240 по ширине)
// Кольцо мощности: рисуется по внешнему краю спидометра, поэтому шкала
// (циферблат) отодвинута внутрь на METER_PAD = ширина кольца + зазор.
#define POWER_ARC_W 6          // толщина синего кольца, px
#define METER_PAD (POWER_ARC_W + 5)
#define POWER_MAX_W 3000       // верх шкалы кольца, Вт (полное кольцо)
#define POWER_ALERT_W 3000.0f  // выше этого порога кольцо и цифра краснеют
#define COLOR_POWER lv_color_hex(0x2196f3)

#define COLOR_ACCENT lv_color_hex(0x51f051)
#define COLOR_DIM lv_color_hex(0x808080)

// Порядок вкладок: свайп влево/вправо листает их по горизонтали.
// Последняя (TAB_DEBUG) существует только при включённом режиме диагностики,
// поэтому реальное количество лежит в tab_count, а не в дефайне.
#define TAB_CURRENT 0
#define TAB_LIMIT 1
#define TAB_DASH 2
#define TAB_TRIP 3
#define TAB_LOCK 4
#define TAB_DEBUG 5
#define TAB_MAX 6

static uint8_t tab_count = 5;

// Код замка. Клавиатура — только 1..9, поэтому нуля в коде быть не может.
#define LOCK_CODE "1987"
#define LOCK_CODE_LEN 4

#define CURRENT_STEP_A 1.0f   // шаг кнопок +/-
#define CURRENT_MAX_A 60.0f   // потолок setCurrent, А (сверь с настройками VESC!)

static void ui_lock_reset_entry(void);

// Точки-индикатор текущей вкладки (кнопки таба спрятаны)
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
  // Уход с клавиатуры сбрасывает недобранный код: вернувшись, начинаем с чистого листа
  if (act != TAB_LOCK) ui_lock_reset_entry();
}

// Приводит подписи/цвета к текущему состоянию лимита
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

// ====== Замок: клавиатура 1..9 и код ======
// Набранное хранится строкой, чтобы сравнение с LOCK_CODE было тривиальным.
static char lock_entry[LOCK_CODE_LEN + 1] = "";
static uint8_t lock_entry_len = 0;

static void ui_lock_update_mask(void) {
  if (!lbl_lock_mask) return;
  // «* * - -»: сколько цифр уже введено
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

// Бейдж на спидометре + подсказка на вкладке замка
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

  // Код набран целиком: тот же код и запирает, и отпирает
  const bool ok = strcmp(lock_entry, LOCK_CODE) == 0;
  ui_lock_reset_entry();
  if (ok) {
    lock_on = !lock_on;
    ui_refresh_lock();  // сам напишет актуальный статус и покажет/спрячет бейдж
  } else {
    ui_lock_set_status("WRONG CODE", lv_palette_main(LV_PALETTE_RED));
  }
}

// ====== Меню настроек (шестерёнка на спидометре) ======
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

// Набор вкладок фиксируется в момент сборки (в LVGL 8.3 нет lv_tabview_remove_tab),
// поэтому переключатель диагностики пересобирает весь UI. Только через lv_async_call:
// удалять дерево из колбэка кнопки, которая в нём же и живёт, нельзя.
static void ui_rebuild_async_cb(void *unused) {
  LV_UNUSED(unused);
  lv_obj_clean(lv_layer_top());  // оверлей настроек живёт там
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

// Двойной тап по спидометру — быстрый переключатель лимита, чтобы не листать
// на вкладку 1 на ходу. Двойного клика в LVGL нет, ловим два CLICKED подряд.
#define DOUBLE_TAP_MS 400
static void ui_meter_click_cb(lv_event_t *e) {
  LV_UNUSED(e);
  static uint32_t last_click = 0;
  uint32_t now = lv_tick_get();
  // last_click == 0 — первый тап за всё время; lv_tick_elaps() учитывает
  // переполнение счётчика тиков.
  if (last_click != 0 && lv_tick_elaps(last_click) < DOUBLE_TAP_MS) {
    ui_set_limit25(!limit25_on);  // сам обновит кнопку на вкладке 1 и бейдж
    last_click = 0;               // третий тап не считаем за новую пару
    return;
  }
  last_click = now;
}

// Подписи тока: крупное «сколько выставлено» + «сколько реально уехало в VESC»
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
  // Кнопка: пока в VESC уехало ровно то, что выставлено, следующее нажатие
  // выключает ток — так и подписываем (OFF, красная). Если setpoint успели
  // покрутить, кнопка снова становится UPDATE и применяет новое значение.
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

// Кнопки «−» и «+»: шаг передаём через user_data. Реагируем и на удержание,
// чтобы не тыкать 60 раз.
static void ui_current_step_cb(lv_event_t *e) {
  const float step = (float)(intptr_t)lv_event_get_user_data(e) * CURRENT_STEP_A;
  float v = current_setpoint + step;
  if (v < 0.0f) v = 0.0f;
  if (v > CURRENT_MAX_A) v = CURRENT_MAX_A;
  current_setpoint = v;
  ui_refresh_current();
}

// UPDATE: только здесь setpoint становится тем, что loop() шлёт в VESC.
// Повторное нажатие (когда applied уже равен setpoint) выключает ток: applied = 0,
// loop() перестаёт слать setCurrent и через таймаут VESC (~1с) газ снова у ручки.
static void ui_current_update_cb(lv_event_t *e) {
  LV_UNUSED(e);
  const bool armed = current_applied > 0.01f &&
                     fabsf(current_applied - current_setpoint) < 0.005f;
  current_applied = armed ? 0.0f : current_setpoint;
  ui_refresh_current();
}

// Один знак после точки со знаком минуса (рекуперация): целое/дробное считаем
// по модулю, иначе значения в диапазоне (-1..0) потеряли бы «-».
static void ui_format_amps(char *buf, size_t n, float a) {
  float mag = fabsf(a);
  int whole = (int)mag;
  int frac = (int)((mag - whole) * 10.0f);
  snprintf(buf, n, "%s%d.%d A", (a < -0.05f) ? "-" : "", whole, frac);
}

// Один лейбл на параметр: во время замера в нём стоит максимум (зелёным),
// в обычном режиме — сглаженное текущее значение (белым).
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

// Лейбл кВт под спидометром: во время замера — максимум (зелёным), иначе
// сглаженная текущая мощность (цветом кольца). Кольцо при этом всегда живое —
// это указатель, а не показание, и по нему видно, что происходит сейчас.
static void ui_refresh_power(void) {
  if (!lbl_power) return;
  const bool peak = peak_measuring;
  // Целые/дробные части руками: %f в lv_snprintf может быть отключён в lv_conf.h
  float kw = (peak ? peak_power_w : power_ema) / 1000.0f;
  bool neg = kw < 0.0f;
  if (neg) kw = -kw;
  int whole = (int)kw;
  int frac = (int)((kw - whole) * 100.0f + 0.5f);
  if (frac >= 100) { frac -= 100; whole += 1; }
  char buf[24];
  lv_snprintf(buf, sizeof(buf), "%s%d.%02d kW", neg ? "-" : "", whole, frac);
  lv_label_set_text(lbl_power, buf);
  // Перебор по мощности важнее «режима замера», поэтому красный перебивает зелёный
  const float shown_w = peak ? peak_power_w : power_ema;
  lv_color_t color = (shown_w > POWER_ALERT_W) ? lv_palette_main(LV_PALETTE_RED)
                                               : (peak ? COLOR_ACCENT : COLOR_POWER);
  lv_obj_set_style_text_color(lbl_power, color, 0);
}

static void ui_peak_btn_cb(lv_event_t *e) {
  LV_UNUSED(e);
  bool on = btn_peak && lv_obj_has_state(btn_peak, LV_STATE_CHECKED);
  // Включение — всегда новый замер с нуля; выключение оставляет цифры на экране.
  if (on && !peak_measuring) {
    peak_motor_a = 0.0f;
    peak_input_a = 0.0f;
    peak_power_w = 0.0f;
  }
  peak_measuring = on;
  ui_refresh_currents();
  ui_refresh_power();
}

// Общий стиль страницы вкладки: чёрный фон, вертикальный флекс, без прокрутки
// (прокрутка нужна только контейнеру самого tabview — им и листаем свайпом).
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

// Строка отчёта: серая подпись сверху, крупное значение под ней.
// Возвращает лейбл значения.
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

// Два компактных показания в один ряд — экономит высоту на вкладке отчёта,
// где полноразмерных строк ui_report_row() больше уже не влезает.
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

// Все указатели на виджеты — статики, а ui_build() может вызываться повторно
// (пересборка при переключении диагностики). Обнуляем их до сборки: часть виджетов
// (вкладка диагностики) существует не всегда, и старые указатели после
// lv_obj_clean() висели бы на удалённых объектах.
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

  // Вкладки листаются свайпом (кнопки таба спрятаны, высота 0):
  // 0 — ток мотора (setCurrent), 1 — лимит 25 км/ч, 2 — спидометр (по умолчанию),
  // 3 — отчёт о пробеге, 4 — замок, 5 — диагностика (только при debug_enabled).
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

  // ====== Самая левая вкладка: ток мотора (setCurrent) ======
  lv_obj_t *lbl_cur_title = lv_label_create(tab_current);
  lv_label_set_text(lbl_cur_title, "MOTOR CURRENT");
  lv_obj_set_style_text_color(lbl_cur_title, COLOR_DIM, 0);
  lv_obj_set_style_text_font(lbl_cur_title, &lv_font_montserrat_18, 0);

  lbl_current_val = lv_label_create(tab_current);
  lv_obj_set_style_text_color(lbl_current_val, lv_color_white(), 0);
  lv_obj_set_style_text_font(lbl_current_val, &lv_font_montserrat_48, 0);

  // Ряд кнопок «−» / «+»
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
    // CLICKED + LONG_PRESSED_REPEAT: удержание крутит значение быстро
    lv_obj_add_event_cb(btn, ui_current_step_cb, LV_EVENT_CLICKED, (void *)(intptr_t)step_dir[i]);
    lv_obj_add_event_cb(btn, ui_current_step_cb, LV_EVENT_LONG_PRESSED_REPEAT,
                        (void *)(intptr_t)step_dir[i]);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, step_txt[i]);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_38, 0);
    lv_obj_center(lbl);
  }

  // UPDATE — отправить выставленное значение в VESC; повторное нажатие выключает ток
  // (цвет и подпись кнопки ведёт ui_refresh_current())
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

  // ====== Вкладка «влево»: ограничение скорости 25 км/ч ======
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
  // Состояние по умолчанию — выключено (LV_STATE_CHECKED не выставляем)

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

  // Не просто refresh: при пересборке UI (тумблер диагностики) кнопка создаётся
  // заново и её LV_STATE_CHECKED надо вернуть из состояния, а не наоборот.
  ui_set_limit25(limit25_on);

  // ====== Вкладка «вправо»: отчёт о пробеге и стоимости (вертикально) ======
  lbl_tachometer = ui_report_row(tab_trip, "TRIP (SINCE BOOT)", "0.0 km");
  lbl_tachometerAbs = ui_report_row(tab_trip, "TOTAL", "0 km");
  lbl_cost = ui_report_row(tab_trip, "COST, ILS", "0.0");
  // Токи снизу: MOTOR — фазный (момент), BATTERY — входной (мощность).
  // Во время замера в этих же лейблах показывается максимум (зелёным).
  ui_report_pair(tab_trip, "MOTOR", "BATTERY", &lbl_cur_motor, &lbl_cur_input);

  // Кнопка замера пиков. Токи скачут быстрее, чем читаются с экрана, поэтому
  // максимум копится в фоне, а не подсматривается глазами.
  btn_peak = lv_btn_create(tab_trip);
  lv_obj_set_size(btn_peak, 200, 38);
  lv_obj_add_flag(btn_peak, LV_OBJ_FLAG_CHECKABLE);
  lv_obj_set_style_radius(btn_peak, 10, 0);
  lv_obj_set_style_bg_color(btn_peak, lv_color_hex(0x2b2b2b), 0);                 // выключено
  lv_obj_set_style_bg_color(btn_peak, lv_color_hex(0x1f7a1f), LV_STATE_CHECKED);  // идёт замер
  lv_obj_set_style_border_width(btn_peak, 2, 0);
  lv_obj_set_style_border_color(btn_peak, lv_color_hex(0x555555), 0);
  lv_obj_set_style_border_color(btn_peak, COLOR_ACCENT, LV_STATE_CHECKED);
  lv_obj_add_event_cb(btn_peak, ui_peak_btn_cb, LV_EVENT_VALUE_CHANGED, NULL);
  // Пересборка UI не должна прерывать идущий замер — возвращаем кнопке состояние
  if (peak_measuring) lv_obj_add_state(btn_peak, LV_STATE_CHECKED);

  // Шестерёнка настроек — в свободном верхнем углу вкладки отчёта. IGNORE_LAYOUT,
  // чтобы не влезать в флекс-колонку страницы (подписи по центру её не задевают).
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

  // ====== Центральная вкладка: спидометр ======
  lv_obj_t *root = tab_dash;

  // ====== Блок 1: Speed (спидометр) ======
  // Спидометр: круглая шкала со стрелкой и цветной дугой
  meter_speed = lv_meter_create(root);
  lv_obj_clear_flag(meter_speed, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(meter_speed, METER_SIZE, METER_SIZE);
  lv_obj_set_style_bg_opa(meter_speed, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(meter_speed, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(meter_speed, METER_PAD, LV_PART_MAIN);
  lv_obj_set_style_text_font(meter_speed, &lv_font_montserrat_12, LV_PART_TICKS);
  // Двойной тап по циферблату включает/выключает лимит 25 км/ч. Свайп между
  // вкладками не ломается: спидометр не прокручиваемый, жест уходит в tabview,
  // а CLICKED после протяжки LVGL не присылает.
  lv_obj_add_flag(meter_speed, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(meter_speed, ui_meter_click_cb, LV_EVENT_CLICKED, NULL);

  lv_meter_scale_t *scale = lv_meter_add_scale(meter_speed);
  // 56 мелких делений (0..55), каждое 5-е крупное с подписью
  lv_meter_set_scale_ticks(meter_speed, scale, 56, 2, 6, lv_color_hex(0x808080));
  lv_meter_set_scale_major_ticks(meter_speed, scale, 5, 3, 11, lv_color_white(), 12);
  // диапазон 0..40 км/ч, дуга 270°, начало под углом 135° (снизу-слева)
  lv_meter_set_scale_range(meter_speed, scale, 0, SPEED_MAX_KMH, 270, 135);

  // Цветная дуга, растущая вместе со скоростью
  arc_speed = lv_meter_add_arc(meter_speed, scale, 5, lv_color_hex(0x51f051), 0);
  lv_meter_set_indicator_start_value(meter_speed, arc_speed, 0);
  lv_meter_set_indicator_end_value(meter_speed, arc_speed, 0);

  // Стрелка
  needle_speed = lv_meter_add_needle_line(meter_speed, scale, 4, lv_color_hex(0x51f051), -8);
  lv_meter_set_indicator_value(meter_speed, needle_speed, 0);

  // Кольцо мощности на колесо: синяя линия по внешнему краю спидометра.
  // Геометрия повторяет шкалу (старт 135°, 270° по часовой), чтобы кольцо
  // читалось как продолжение циферблата. Ребёнок спидометра — так центр
  // совпадает автоматически, без ручного выравнивания.
  arc_power = lv_arc_create(meter_speed);
  lv_obj_remove_style(arc_power, NULL, LV_PART_KNOB);  // ручку не показываем
  lv_obj_set_size(arc_power, METER_SIZE - 2, METER_SIZE - 2);
  lv_obj_center(arc_power);
  // Не перехватываем касания: двойной тап по циферблату и свайп вкладок
  // должны доходить до спидометра/tabview под кольцом.
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

  // Число скорости в центре спидометра
  lbl_speed = lv_label_create(meter_speed);
  lv_label_set_text(lbl_speed, "--");
  lv_obj_set_style_text_color(lbl_speed, lv_color_white(), 0);
  lv_obj_set_style_text_font(lbl_speed, &lv_font_montserrat_48, 0);
  lv_obj_set_style_text_align(lbl_speed, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(lbl_speed, LV_ALIGN_CENTER, 0, -10);

  // Подпись "km/h" под числом
  lv_obj_t *lbl_speed_unit = lv_label_create(meter_speed);
  lv_label_set_text(lbl_speed_unit, "km/h");
  lv_obj_set_style_text_color(lbl_speed_unit, lv_color_hex(0xaaaaaa), 0);
  lv_obj_set_style_text_font(lbl_speed_unit, &lv_font_montserrat_14, 0);
  lv_obj_align(lbl_speed_unit, LV_ALIGN_CENTER, 0, 28);

  // Мощность в кВт — в нижнем разрыве циферблата (там шкалы нет), цветом кольца
  lbl_power = lv_label_create(meter_speed);
  lv_label_set_text(lbl_power, "0.00 kW");
  lv_obj_set_style_text_color(lbl_power, COLOR_POWER, 0);
  lv_obj_set_style_text_font(lbl_power, &lv_font_montserrat_18, 0);
  // +58: ниже подписей крайних делений шкалы (0 и 55), чтобы не наезжать на них
  lv_obj_align(lbl_power, LV_ALIGN_CENTER, 0, 58);

  // ====== Блок 2: строка Battery % / Voltage / Temp ======
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
                        LV_FLEX_ALIGN_SPACE_BETWEEN,  // по X
                        LV_FLEX_ALIGN_CENTER,         // по Y
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

  // Температура контроллера (подпись "Temp" убрали — в 240px ширины не нужна)
  lbl_temp_val = lv_label_create(info);
  lv_label_set_text(lbl_temp_val, "--°C");
  lv_obj_set_style_text_color(lbl_temp_val, lv_color_white(), 0);
  lv_obj_set_style_text_font(lbl_temp_val, &lv_font_montserrat_22, 0);

  // Бейджи активных режимов — видны на спидометре, чтобы состояние
  // не приходилось искать на соседних вкладках
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

  // Замочек: в LVGL 8.3 среди встроенных символов замка нет, поэтому словом
  lbl_lock_badge = lv_label_create(badges);
  lv_label_set_text(lbl_lock_badge, "LOCKED");
  lv_obj_set_style_text_color(lbl_lock_badge, lv_palette_main(LV_PALETTE_RED), 0);
  lv_obj_set_style_text_font(lbl_lock_badge, &lv_font_montserrat_14, 0);
  lv_obj_add_flag(lbl_lock_badge, LV_OBJ_FLAG_HIDDEN);

  // ====== Вкладка замка: клавиатура 1..9 ======
  // Заголовок и маска сверху заодно оставляют полосу, с которой удобно начать
  // свайп — сетка кнопок на всю страницу мешала бы листать вкладки.
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

  // ====== Вкладка диагностики (только при включённом режиме) ======
  if (tab_diag) {
    lbl_diag_fault = ui_report_row(tab_diag, "VESC FAULT", "NONE");
    lbl_diag_last = ui_report_row(tab_diag, "LAST FAULT (SINCE BOOT)", "NONE");
    // Имена аварий длинные («GATE DRV UNDER VOLT») — переносим по словам,
    // иначе строка уезжает за края 240-пиксельного экрана.
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

  // ====== Меню настроек: оверлей поверх всего (включая точки вкладок) ======
  settings_modal = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(settings_modal);
  lv_obj_set_size(settings_modal, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(settings_modal, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(settings_modal, LV_OPA_70, 0);
  // Кликабельная подложка: гасит нажатия по UI под меню
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

  // Тумблер диагностики: добавляет/убирает последнюю вкладку (через пересборку UI)
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

  // ====== Индикатор текущей вкладки (точки внизу) ======
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

  // Стартуем на спидометре: влево — лимит и ток, вправо — отчёт
  lv_tabview_set_act(tabview, TAB_DASH, LV_ANIM_OFF);
  ui_update_dots(TAB_DASH);
}

// Хелперы для обновления значений:
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

// Текущее (анимируемое) положение стрелки в десятых долях км/ч.
// Шкала спидометра целочисленная (0..SPEED_MAX_KMH), поэтому храним ×10,
// чтобы анимация шла плавно и без «ступенек».
static int32_t needle_cur_x10 = 0;
// Сглаженная скорость (экспоненциальное среднее) — гасит шум телеметрии VESC.
static float speed_ema = 0.0f;
static bool speed_ema_init = false;

// Колбэк анимации: получает значение в десятых км/ч, двигает стрелку и дугу.
static void speed_anim_cb(void *var, int32_t x10) {
  needle_cur_x10 = x10;
  int32_t v = (x10 + 5) / 10;  // округляем до целого км/ч для шкалы
  if (meter_speed && needle_speed) {
    lv_meter_set_indicator_value(meter_speed, needle_speed, v);
  }
  if (meter_speed && arc_speed) {
    lv_meter_set_indicator_end_value(meter_speed, arc_speed, v);
  }
}

void ui_set_speed(float rpm) {
  if (!lbl_speed) return;
  float speed = erpm_to_kmh(rpm, POLE_PAIRS, WHEEL_CIRC_M);  // или "%.0f km/h"

  // Сглаживаем скорость, чтобы стрелка не дёргалась от шума телеметрии.
  if (!speed_ema_init) {
    speed_ema = speed;
    speed_ema_init = true;
  } else {
    speed_ema += 0.35f * (speed - speed_ema);  // коэффициент 0.35: компромисс отклик/плавность
  }
  speed = speed_ema;

  char buf[16];
  int whole = (int)speed;
  int frac = (int)((speed - whole) * 10.0f);
  sprintf(buf, "%d.%d", whole, abs(frac));
  lv_label_set_text(lbl_speed, buf);

  // Выше порога — весь циферблат (цифра, стрелка, дуга) красный, иначе зелёный/белый.
  lv_color_t speed_color = (speed > SPEED_ALERT_KMH) ? lv_palette_main(LV_PALETTE_RED)
                                                      : lv_color_hex(0x51f051);
  lv_obj_set_style_text_color(lbl_speed, (speed > SPEED_ALERT_KMH) ? lv_palette_main(LV_PALETTE_RED)
                                                                   : lv_color_white(),
                              0);
  if (needle_speed) needle_speed->type_data.needle_line.color = speed_color;
  if (arc_speed) arc_speed->type_data.arc.color = speed_color;
  if (meter_speed) lv_obj_invalidate(meter_speed);

  // Целевое положение стрелки в десятых км/ч, с клампом по диапазону шкалы.
  int32_t target_x10 = (int32_t)(speed * 10.0f + 0.5f);
  if (target_x10 < 0) target_x10 = 0;
  if (target_x10 > SPEED_MAX_KMH * 10) target_x10 = SPEED_MAX_KMH * 10;

  if (meter_speed && needle_speed) {
    // Плавно доводим стрелку от текущего положения к целевому.
    // Повторный старт с тем же (var, exec_cb) заменяет предыдущую анимацию,
    // поэтому стрелка всегда «догоняет» последнее значение без рывков.
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, meter_speed);
    lv_anim_set_exec_cb(&a, speed_anim_cb);
    lv_anim_set_values(&a, needle_cur_x10, target_x10);
    lv_anim_set_time(&a, 300);  // чуть длиннее интервала опроса — движение непрерывное
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
  int whole = (int)celsius;                         // целая часть
  int frac = (int)fabs((celsius - whole) * 10.0f);  // 1 цифра после точки
  // ° может не быть в шрифте, тогда просто " C"
  sprintf(buf, "%d.%d°C", whole, frac);
  // sprintf(buf, "%d.%d C", whole, frac); // если нет глифа °
  lv_label_set_text(lbl_temp_val, buf);
}

// Подписи на вкладке отчёта — только значения (заголовки статичные).
void ui_set_tachometer(float tachometer) {
  if (!lbl_tachometer) return;
  float km = ((tachometer / (POLE_PAIRS * 2 * 3)) * WHEEL_CIRC_M) / 1000.0;
  char buf[48];
  int whole = (int)km;                         // целая часть
  int frac = (int)fabs((km - whole) * 10.0f);  // 1 цифра после точки
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
  // Шекель ₪ (U+20AA) отсутствует в Montserrat — пишем без символа валюты.
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

  // Пик — по СЫРОМУ значению (сглаживание срезает как раз то, что замеряем),
  // тем же принципом, что и пиковые токи в ui_set_currents().
  if (peak_measuring && watts > peak_power_w) peak_power_w = watts;

  if (arc_power) {
    // Рекуперацию (минус) кольцом не показываем — только подписью.
    int32_t v = (int32_t)(power_ema + (power_ema >= 0 ? 0.5f : -0.5f));
    if (v < 0) v = 0;
    if (v > POWER_MAX_W) v = POWER_MAX_W;
    lv_arc_set_value(arc_power, v);
    // Кольцо всегда живое (даже во время замера), поэтому цвет — по текущей
    // мощности: полное красное кольцо = вышли за POWER_ALERT_W.
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

  // Пики ловим по СЫРЫМ значениям: сглаживание срезает как раз то, что мы
  // пытаемся замерить. Отрицательные (рекуперация) игнорируем — нужен максимум
  // подаваемого тока.
  if (peak_measuring) {
    if (motor_a > peak_motor_a) peak_motor_a = motor_a;
    if (input_a > peak_input_a) peak_input_a = input_a;
  }

  ui_refresh_currents();
}

// ===== Ограничение скорости 25 км/ч =====
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

// ===== Замок =====
bool ui_get_lock(void) {
  return lock_on;
}

// Вызывается на старте с сохранённым в NVS состоянием
void ui_set_lock(bool on) {
  lock_on = on;
  ui_lock_reset_entry();
  ui_refresh_lock();
}

// ===== Режим диагностики =====
bool ui_get_debug_enabled(void) {
  return debug_enabled;
}

void ui_set_debug_enabled(bool on) {
  debug_enabled = on;
  ui_refresh_debug_btn();  // сама вкладка появится при ближайшей сборке UI
}

// Имена кодов аварий VESC. Порядок = mc_fault_code из datatypes.h библиотеки
// VescUart (сам заголовок сюда не тянем: ui.cpp собирается и PC-симулятором).
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

// Последняя ненулевая авария с момента загрузки: код в телеметрии сам сбрасывается,
// когда причина ушла, — без защёлки коротких аварий просто не увидеть.
static int last_fault_code = 0;

static void ui_fault_name(char *buf, size_t n, int code) {
  if (code >= 0 && code < FAULT_NAMES_N) lv_snprintf(buf, n, "%s", FAULT_NAMES[code]);
  else lv_snprintf(buf, n, "FAULT #%d", code);
}

void ui_set_diag(int fault_code, float duty, float temp_motor, bool comm_ok) {
  if (fault_code != 0) last_fault_code = fault_code;
  if (!lbl_diag_fault) return;  // вкладка диагностики не построена

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

// ===== Ток мотора (setCurrent) =====
// Значение, подтверждённое кнопкой UPDATE. loop() обязан повторять его в VESC:
// у контроллера таймаут команды ~1 с, одиночная посылка просто затухнет.
float ui_get_current_applied(void) {
  return current_applied;
}

// Последняя показанная (сглаженная) скорость — по ней и решаем, давить ли лимит,
// чтобы поведение совпадало с тем, что человек видит на спидометре.
float ui_speed_kmh(void) {
  return speed_ema_init ? speed_ema : 0.0f;
}

// Обратное преобразование к erpm_to_kmh() с ТЕМИ ЖЕ константами, что у спидометра
// (в utils.cpp kmh_to_erpm() считает по другим POLE_PAIRS/колесу — для лимита не годится).
float ui_kmh_to_erpm(float kmh) {
  return (kmh * 1000.0f / 60.0f / WHEEL_CIRC_M) * (float)POLE_PAIRS;
}
