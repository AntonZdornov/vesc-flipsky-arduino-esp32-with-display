#include <Arduino.h>
#include <HardwareSerial.h>
#include "VescUart.h"
#include "display.h"
#include "lvgl_driver.h"
#include "logger.h"
#include "utils.h"
#include "ui_globals.h"
#include <lvgl.h>

unsigned long lastFetch = 0;

static lv_obj_t *lbl_batt = NULL;
static lv_obj_t *lbl_volt = NULL;
static lv_obj_t *lbl_speed = NULL;
static lv_obj_t *lbl_temp = NULL;
static lv_obj_t *lbl_temp_val = NULL;
static lv_obj_t *lbl_debbug = NULL;


extern const lv_font_t lv_font_montserrat_48;
extern const lv_font_t lv_font_montserrat_38;
extern const lv_font_t lv_font_montserrat_22;
extern const lv_font_t lv_font_montserrat_18;
extern const lv_font_t lv_font_montserrat_12;

#define VESC_RX_PIN 3  // VESC RX
#define VESC_TX_PIN 2  // VESC TX
#define BAUD 115200
HardwareSerial VSerial(1);  // создаём второй UART
VescUart UART;

#define POLE_PAIRS 15  // обычно 7 для 14-полюсного мотора
#define WHEEL_DIAMETER_M 0.255
#define WHEEL_CIRC_M (3.1415926f * WHEEL_DIAMETER_M)

void ui_build(void) {
  // lv_obj_clean(lv_scr_act());
  // lv_refr_now(NULL);

  // Корневой контейнер на весь экран (у тебя 320x172)
  lv_obj_t *body = lv_obj_create(lv_scr_act());
  // lv_disp_t *d = lv_disp_get_default();
  // lv_obj_set_size(root, lv_disp_get_hor_res(d), lv_disp_get_ver_res(d));
  lv_obj_set_size(body, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_pad_all(body, 2, LV_PART_MAIN);
  lv_obj_set_style_bg_color(body, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(body, LV_OPA_COVER, LV_PART_MAIN);
  // lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

  // Горизонтальный флекс: 3 колонки
  // lv_obj_set_layout(root, LV_LAYOUT_FLEX);
  // lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
  // lv_obj_set_flex_align(root,
  //                       LV_FLEX_ALIGN_START,    // по X
  //                       LV_FLEX_ALIGN_CENTER,   // по Y
  //                       LV_FLEX_ALIGN_CENTER);  // между линиями

  // lv_obj_t *body = lv_obj_create(root);

  lv_obj_set_layout(body, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(body,
                        LV_FLEX_ALIGN_START,    // по X
                        LV_FLEX_ALIGN_CENTER,   // по Y
                        LV_FLEX_ALIGN_CENTER); 

  // lv_obj_t *footer = lv_obj_create(root);
  //   lv_obj_set_style_bg_opa(footer, LV_OPA_TRANSP, 0);
  // lv_obj_set_style_border_width(footer, 0, LV_PART_MAIN);
  // lv_obj_set_style_pad_all(footer, 0, 0);
  // lv_obj_set_size(footer, LV_SIZE_CONTENT, LV_PCT(100));
  // lbl_debbug = lv_label_create(footer);
  // lv_label_set_text(lbl_debbug, "Debbuger:");
  // lv_obj_set_style_text_color(lbl_debbug, lv_color_white(), 0);
  // lv_obj_set_style_text_font(lbl_debbug, &lv_font_montserrat_12, 0);
  // lv_obj_set_width(lbl_debbug, LV_PCT(100));
  // lv_obj_set_style_text_align(lbl_debbug, LV_TEXT_ALIGN_CENTER, 0);
  

  // ====== Колонка 1: Battery / Voltage ======
  lv_obj_t *col1 = lv_obj_create(body);
  lv_obj_set_style_bg_opa(col1, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(col1, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(col1, 0, 0);
  lv_obj_set_size(col1, LV_SIZE_CONTENT, LV_PCT(100));
  lv_obj_set_layout(col1, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(col1, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(col1, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_flex_grow(col1, 1, 0);  // пропорции 1 : 2 : 1

  // Battery %
  lbl_batt = lv_label_create(col1);
  lv_label_set_text(lbl_batt, "--%");
  lv_obj_set_style_text_color(lbl_batt, lv_color_white(), 0);
  lv_obj_set_style_text_font(lbl_batt, &lv_font_montserrat_22, 0);
  lv_obj_set_width(lbl_batt, LV_PCT(100));
  lv_obj_set_style_text_align(lbl_batt, LV_TEXT_ALIGN_CENTER, 0);

  // Voltage
  lbl_volt = lv_label_create(col1);
  lv_label_set_text(lbl_volt, "--.-V");
  lv_obj_set_style_text_color(lbl_volt, lv_color_white(), 0);
  lv_obj_set_style_text_font(lbl_volt, &lv_font_montserrat_22, 0);
  lv_obj_set_width(lbl_volt, LV_PCT(100));
  lv_obj_set_style_text_align(lbl_volt, LV_TEXT_ALIGN_CENTER, 0);

  // ====== Колонка 2 (центр): Speed ======
  lv_obj_t *col2 = lv_obj_create(body);
  lv_obj_set_style_bg_opa(col2, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(col2, 0, 0);
  lv_obj_set_style_pad_all(col2, 0, 0);
  lv_obj_set_size(col2, LV_SIZE_CONTENT, LV_PCT(100));
  lv_obj_set_layout(col2, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(col2, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(col2, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_flex_grow(col2, 2, 0);  // центральная шире

  // Speed (крупно, "км/ч" рядом или ниже — как нравится)
  lbl_speed = lv_label_create(col2);
  lv_label_set_text(lbl_speed, "KM/H\n--");  // или "32 km/h"
  lv_obj_set_style_text_color(lbl_speed, lv_color_white(), 0);
  lv_obj_set_style_text_font(lbl_speed, &lv_font_montserrat_38, 0);
  lv_obj_set_width(lbl_speed, LV_PCT(100));
  lv_obj_set_style_text_align(lbl_speed, LV_TEXT_ALIGN_CENTER, 0);

  // ====== Колонка 3: Temp ======
  lv_obj_t *col3 = lv_obj_create(body);
  lv_obj_set_style_bg_opa(col3, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(col3, 0, 0);
  lv_obj_set_style_pad_all(col3, 0, 0);
  lv_obj_set_size(col3, LV_SIZE_CONTENT, LV_PCT(100));
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

  char buf1[32];
  lv_snprintf(buf1, sizeof(buf1), "%u%%", percent);
  lv_label_set_text(lbl_batt, buf1);

  char buf[16];
  int whole = (int)volts;
  int frac = (int)((volts - whole) * 10.0f);
  sprintf(buf, "%d.%dV", whole, abs(frac));
  lv_label_set_text(lbl_volt, buf);
}

void ui_set_speed(float rpm) {
  if (!lbl_speed) return;
  float speed = erpm_to_kmh(rpm, POLE_PAIRS, WHEEL_CIRC_M);  // или "%.0f km/h"
  char buf[16];
  int whole = (int)speed;
  int frac = (int)((speed - whole) * 10.0f);
  sprintf(buf, "KM/H\n%d.%d", whole, abs(frac));
  lv_label_set_text(lbl_speed, buf);
}

void ui_set_temp(float celsius) {
  if (!lbl_temp_val) return;

  char buf[16];
  int whole = (int)celsius;                         // целая часть
  int frac = (int)fabs((celsius - whole) * 10.0f);  // 1 цифра после точки
  // ° может не быть в шрифте, тогда просто " C"
  sprintf(buf, "%d.%d°C", whole, frac);
  // sprintf(buf, "%d.%d C", whole, frac); // если нет глифа °
  lv_label_set_text(lbl_temp_val, buf);
}

void setup() {
  LOG_BEGIN(BAUD);
  delay(200);
  LCD_Init();
  Lvgl_Init();
  ui_build();

  VSerial.begin(BAUD, SERIAL_8N1, VESC_RX_PIN, VESC_TX_PIN);
  UART.setSerialPort(&VSerial);

  delay(200);
}

void loop() {
  Timer_Loop();
  if (UART.getVescValues()) {
    float voltage = UART.data.inpVoltage;
    float temp = UART.data.tempMosfet;
    float rpm = UART.data.rpm;
    ui_set_battery(voltage);
    ui_set_temp(temp);
    ui_set_speed(rpm);
  }

  delay(500);
}
