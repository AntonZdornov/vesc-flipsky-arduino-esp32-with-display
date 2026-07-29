#include "ui.h"

#include <lvgl.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

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
static lv_obj_t *lbl_temp = NULL;
static lv_obj_t *lbl_temp_val = NULL;
static lv_obj_t *lbl_tachometer = NULL;
static lv_obj_t *lbl_tachometerAbs = NULL;
static lv_obj_t *lbl_cost = NULL;
static lv_obj_t *lbl_debbug = NULL;

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

void ui_build(void) {
  // lv_obj_clean(lv_scr_act());
  // lv_refr_now(NULL);

  // Корневой контейнер на весь экран (у тебя 320x172)
  lv_obj_t *root = lv_obj_create(lv_scr_act());
  lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
  // lv_disp_t *d = lv_disp_get_default();
  // lv_obj_set_size(root, lv_disp_get_hor_res(d), lv_disp_get_ver_res(d));
  lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_pad_all(root, 2, LV_PART_MAIN);
  lv_obj_set_style_pad_bottom(root, 8, LV_PART_MAIN);
  lv_obj_set_style_bg_color(root, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(root, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_opa(root, LV_OPA_TRANSP, LV_PART_MAIN);
  // Горизонтальный флекс: 3 колонки
  lv_obj_set_layout(root, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(root,
                        LV_FLEX_ALIGN_CENTER,   // по X
                        LV_FLEX_ALIGN_CENTER,   // по Y
                        LV_FLEX_ALIGN_CENTER);  // между линиями

  lv_obj_t *body = lv_obj_create(root);
  lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_opa(body, LV_OPA_TRANSP, LV_PART_MAIN);
  // lv_obj_set_style_outline_opa(body, LV_OPA_TRANSP, LV_PART_MAIN);
  // lv_obj_set_style_shadow_opa(body, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_size(body, LV_PCT(100), LV_PCT(80));
  lv_obj_set_style_pad_all(body, 0, LV_PART_MAIN);

  lv_obj_set_style_bg_color(body, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_layout(body, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(body,
                        LV_FLEX_ALIGN_CENTER,  // по X
                        LV_FLEX_ALIGN_CENTER,  // по Y
                        LV_FLEX_ALIGN_CENTER);

  lv_obj_t *footer = lv_obj_create(root);
  lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(footer, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(footer, 2, LV_PART_MAIN);
  lv_obj_set_style_border_color(footer, lv_color_hex(0x51f051), LV_PART_MAIN);
  lv_obj_set_style_pad_left(footer, 4, LV_PART_MAIN);
  lv_obj_set_style_pad_right(footer, 4, LV_PART_MAIN);
  lv_obj_set_size(footer, LV_PCT(100), LV_PCT(20));
  lv_obj_set_layout(footer, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(footer, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(footer,
                        LV_FLEX_ALIGN_SPACE_BETWEEN,  // по X
                        LV_FLEX_ALIGN_CENTER,         // по Y
                        LV_FLEX_ALIGN_CENTER);

  if (DEBUG_MODE == 1) {
    lbl_debbug = lv_label_create(footer);
    lv_label_set_text(lbl_debbug, "Debug mode:");
    lv_obj_set_style_text_color(lbl_debbug, lv_color_make(255, 0, 0), 0);
    lv_obj_set_style_text_font(lbl_debbug, &lv_font_montserrat_14, 0);
    lv_obj_set_width(lbl_debbug, LV_PCT(100));
    lv_obj_set_style_text_align(lbl_debbug, LV_TEXT_ALIGN_LEFT, 0);
  } else {
    lbl_tachometer = lv_label_create(footer);
    lv_label_set_text(lbl_tachometer, "Trip: 0km");
    lv_obj_set_style_text_color(lbl_tachometer, lv_color_white(), 0);
    lv_obj_set_style_text_font(lbl_tachometer, &lv_font_montserrat_14, 0);
    lv_obj_set_width(lbl_tachometer, LV_PCT(32));
    lv_obj_set_style_text_align(lbl_tachometer, LV_TEXT_ALIGN_LEFT, 0);

    lbl_tachometerAbs = lv_label_create(footer);
    lv_label_set_text(lbl_tachometerAbs, "Total: 0km");
    lv_obj_set_style_text_color(lbl_tachometerAbs, lv_color_white(), 0);
    lv_obj_set_style_text_font(lbl_tachometerAbs, &lv_font_montserrat_14, 0);
    lv_obj_set_width(lbl_tachometerAbs, LV_PCT(32));
    lv_obj_set_style_text_align(lbl_tachometerAbs, LV_TEXT_ALIGN_LEFT, 0);

    lbl_cost = lv_label_create(footer);
    lv_label_set_text(lbl_cost, "Cost: 0.0");
    lv_obj_set_style_text_color(lbl_cost, lv_color_white(), 0);
    lv_obj_set_style_text_font(lbl_cost, &lv_font_montserrat_14, 0);
    lv_obj_set_width(lbl_cost, LV_PCT(32));
    lv_obj_set_style_text_align(lbl_cost, LV_TEXT_ALIGN_LEFT, 0);
  }

  // ====== Колонка 1: Battery / Voltage ======
  lv_obj_t *col1 = lv_obj_create(body);
  lv_obj_set_style_bg_opa(col1, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(col1, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(col1, 0, 0);
  lv_obj_set_size(col1, LV_SIZE_CONTENT, LV_PCT(90));
  lv_obj_set_layout(col1, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(col1, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(col1, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_flex_grow(col1, 1, 0);  // пропорции 1 : 2 : 1

  // Battery %
  lbl_batt = lv_label_create(col1);
  lv_label_set_text(lbl_batt, "--%");
  lv_obj_set_style_text_color(lbl_batt, lv_color_white(), 0);
  lv_obj_set_style_text_font(lbl_batt, &lv_font_montserrat_26, 0);
  lv_obj_set_width(lbl_batt, LV_PCT(100));
  lv_obj_set_style_text_align(lbl_batt, LV_TEXT_ALIGN_CENTER, 0);

  // Voltage
  lbl_volt = lv_label_create(col1);
  lv_label_set_text(lbl_volt, "--.-V");
  lv_obj_set_style_text_color(lbl_volt, lv_color_white(), 0);
  lv_obj_set_style_text_font(lbl_volt, &lv_font_montserrat_22, 0);
  lv_obj_set_width(lbl_volt, LV_PCT(100));
  lv_obj_set_style_text_align(lbl_volt, LV_TEXT_ALIGN_CENTER, 0);

  // ====== Колонка 2 (центр): Speed (спидометр) ======
  lv_obj_t *col2 = lv_obj_create(body);
  lv_obj_clear_flag(col2, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(col2, 0, LV_PART_MAIN);  // рамку убрали
  lv_obj_set_style_bg_opa(col2, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(col2, 0, LV_PART_MAIN);
  lv_obj_set_size(col2, LV_SIZE_CONTENT, LV_PCT(100));
  lv_obj_set_layout(col2, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(col2, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(col2, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_flex_grow(col2, 2, 0);  // центральная шире

  // Спидометр: круглая шкала со стрелкой и цветной дугой
  meter_speed = lv_meter_create(col2);
  lv_obj_clear_flag(meter_speed, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(meter_speed, 128, 128);
  lv_obj_set_style_bg_opa(meter_speed, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(meter_speed, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(meter_speed, 2, LV_PART_MAIN);
  lv_obj_set_style_text_font(meter_speed, &lv_font_montserrat_12, LV_PART_TICKS);

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

  // Число скорости в центре спидометра
  lbl_speed = lv_label_create(meter_speed);
  lv_label_set_text(lbl_speed, "--");
  lv_obj_set_style_text_color(lbl_speed, lv_color_white(), 0);
  lv_obj_set_style_text_font(lbl_speed, &lv_font_montserrat_38, 0);
  lv_obj_set_style_text_align(lbl_speed, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(lbl_speed, LV_ALIGN_CENTER, 0, -6);

  // Подпись "km/h" под числом
  lv_obj_t *lbl_speed_unit = lv_label_create(meter_speed);
  lv_label_set_text(lbl_speed_unit, "km/h");
  lv_obj_set_style_text_color(lbl_speed_unit, lv_color_hex(0xaaaaaa), 0);
  lv_obj_set_style_text_font(lbl_speed_unit, &lv_font_montserrat_12, 0);
  lv_obj_align(lbl_speed_unit, LV_ALIGN_CENTER, 0, 24);

  // ====== Колонка 3: Temp ======
  lv_obj_t *col3 = lv_obj_create(body);
  lv_obj_set_style_bg_opa(col3, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(col3, 0, 0);
  lv_obj_set_style_pad_all(col3, 0, 0);
  lv_obj_set_size(col3, LV_SIZE_CONTENT, LV_PCT(90));
  lv_obj_set_layout(col3, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(col3, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(col3, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_flex_grow(col3, 1, 0);

  // "Temp" (лейбл)
  lbl_temp = lv_label_create(col3);
  lv_label_set_text(lbl_temp, "Temp");
  lv_obj_set_style_text_color(lbl_temp, lv_color_white(), 0);
  lv_obj_set_style_text_font(lbl_temp, &lv_font_montserrat_22, 0);
  lv_obj_set_width(lbl_temp, LV_PCT(100));
  lv_obj_set_style_text_align(lbl_temp, LV_TEXT_ALIGN_CENTER, 0);

  // Значение температуры контроллера
  lbl_temp_val = lv_label_create(col3);
  lv_label_set_text(lbl_temp_val, "--°C");
  lv_obj_set_style_text_color(lbl_temp_val, lv_color_white(), 0);
  lv_obj_set_style_text_font(lbl_temp_val, &lv_font_montserrat_18, 0);
  lv_obj_set_width(lbl_temp_val, LV_PCT(100));
  lv_obj_set_style_text_align(lbl_temp_val, LV_TEXT_ALIGN_CENTER, 0);
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

void ui_set_tachometer(float tachometer) {
  if (!lbl_tachometer) return;
  float km = ((tachometer / (POLE_PAIRS * 2 * 3)) * WHEEL_CIRC_M) / 1000.0;
  char buf[48];
  int whole = (int)km;                         // целая часть
  int frac = (int)fabs((km - whole) * 10.0f);  // 1 цифра после точки
  sprintf(buf, "Trip: %d.%dkm", whole, frac);
  lv_label_set_text(lbl_tachometer, buf);
}

void ui_set_tachometerAbs(float tachometer) {
  if (!lbl_tachometerAbs) return;
  float km = ((tachometer / (POLE_PAIRS * 2 * 3)) * WHEEL_CIRC_M) / 1000.0;
  char buf[48];
  int whole = (int)km;                         // целая часть
  int frac = (int)fabs((km - whole) * 10.0f);  // 1 цифра после точки
  sprintf(buf, "Total: %dkm", whole, frac);
  lv_label_set_text(lbl_tachometerAbs, buf);
}

void ui_set_cost(float ils) {
  if (!lbl_cost) return;
  char buf[32];
  int whole = (int)ils;
  int frac = (int)fabs((ils - whole) * 10.0f);
  // Шекель ₪ (U+20AA) отсутствует в Montserrat — пишем без символа валюты.
  sprintf(buf, "Cost: %d.%d", whole, frac);
  lv_label_set_text(lbl_cost, buf);
}
