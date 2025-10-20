#include <Arduino.h>
#include <HardwareSerial.h>
#include <VescUart.h>
#include "display.h"
#include "lvgl_driver.h"
#include "logger.h"
#include "utils.h"
#include "ui_globals.h"
#include <lvgl.h>

unsigned long lastFetch = 0;

lv_obj_t *root_container;
lv_obj_t *label_views = nullptr;
lv_obj_t *label_subscribers = nullptr;
extern const lv_font_t lv_font_montserrat_48;
extern const lv_font_t lv_font_montserrat_38;
extern const lv_font_t lv_font_montserrat_12;
extern const lv_font_t lv_font_montserrat_22;

#define VESC_RX_PIN 4  // VESC RX
#define VESC_TX_PIN 5  // VESC TX
#define BAUD 115200
HardwareSerial VSerial(1);  // создаём второй UART
VescUart UART;

void createUI() {
  root_container = lv_obj_create(lv_scr_act());
  lv_obj_clean(root_container);                               // очищаем детей
  lv_obj_set_size(root_container, LV_PCT(100), LV_PCT(100));  // под размер экрана

  // Убираем отступы и границы
  lv_obj_set_style_pad_all(root_container, 0, LV_PART_MAIN);
  lv_obj_set_style_border_width(root_container, 0, LV_PART_MAIN);

  // Задаем черный фон
  lv_obj_set_style_bg_color(root_container, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(root_container, LV_OPA_COVER, LV_PART_MAIN);

  // Раскладка флекс колонкой, выравнивание вниз по центру
  lv_obj_set_layout(root_container, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(root_container, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(root_container,
                        LV_FLEX_ALIGN_CENTER,         // горизонтально по центру
                        LV_FLEX_ALIGN_SPACE_BETWEEN,  // вертикально вниз
                        LV_FLEX_ALIGN_CENTER);

  lv_obj_t *views_group = lv_obj_create(root_container);
  lv_obj_set_height(views_group, LV_PCT(20));  // или LV_SIZE_CONTENT + grow
  lv_obj_set_width(views_group, LV_PCT(100));  // если нужно
  lv_obj_set_style_pad_all(views_group, 0, LV_PART_MAIN);
  lv_obj_set_flex_grow(views_group, 1);

  lv_obj_set_layout(views_group, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(views_group, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(views_group,
                        LV_FLEX_ALIGN_CENTER,  // по горизонтали
                        LV_FLEX_ALIGN_CENTER,  // по вертикали
                        LV_FLEX_ALIGN_CENTER);

  lv_obj_set_style_bg_color(views_group, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(views_group, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_pad_all(views_group, 0, LV_PART_MAIN);
  lv_obj_set_style_border_width(views_group, 0, LV_PART_MAIN);

  lv_obj_t *label_views_title = lv_label_create(views_group);
  lv_label_set_text(label_views_title, "Views");
  lv_obj_set_width(label_views_title, LV_PCT(100));
  lv_obj_set_style_text_align(label_views_title, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(label_views_title, lv_color_white(), 0);
  lv_obj_set_style_text_font(label_views_title, &lv_font_montserrat_22, 0);
  lv_obj_set_style_pad_top(label_views_title, 5, LV_PART_MAIN);  // Отступ сверху

  label_views = lv_label_create(views_group);
  lv_label_set_text(label_views, "0");
  lv_obj_set_width(label_views, LV_PCT(100));
  lv_obj_set_style_text_align(label_views, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(label_views, lv_color_white(), 0);
  lv_obj_set_style_text_font(label_views, &lv_font_montserrat_38, 0);

  // lv_obj_t *subscribers_group = lv_obj_create(root_container);
  // lv_obj_set_height(subscribers_group, LV_PCT(20));  // или LV_SIZE_CONTENT + grow
  // lv_obj_set_width(subscribers_group, LV_PCT(100));  // если нужно
  // lv_obj_set_style_pad_all(subscribers_group, 0, LV_PART_MAIN);
  // lv_obj_set_flex_grow(subscribers_group, 1);

  // lv_obj_set_layout(subscribers_group, LV_LAYOUT_FLEX);
  // lv_obj_set_flex_flow(subscribers_group, LV_FLEX_FLOW_COLUMN);
  // lv_obj_set_flex_align(subscribers_group,
  //                       LV_FLEX_ALIGN_CENTER,  // по горизонтали
  //                       LV_FLEX_ALIGN_CENTER,  // по вертикали
  //                       LV_FLEX_ALIGN_CENTER);

  // lv_obj_set_style_bg_color(subscribers_group, lv_color_black(), LV_PART_MAIN);
  // lv_obj_set_style_bg_opa(subscribers_group, LV_OPA_COVER, LV_PART_MAIN);
  // lv_obj_set_style_pad_all(subscribers_group, 0, LV_PART_MAIN);
  // lv_obj_set_style_border_width(subscribers_group, 0, LV_PART_MAIN);

  // lv_obj_t *label_subscribers_title = lv_label_create(views_group);
  // lv_label_set_text(label_subscribers_title, "Subscribers");
  // lv_obj_set_width(label_subscribers_title, LV_PCT(100));
  // lv_obj_set_style_text_align(label_subscribers_title, LV_TEXT_ALIGN_CENTER, 0);
  // lv_obj_set_style_text_color(label_subscribers_title, lv_color_white(), 0);
  // lv_obj_set_style_text_font(label_subscribers_title, &lv_font_montserrat_22, 0);
  // lv_obj_set_style_pad_top(label_subscribers_title, 5, LV_PART_MAIN);  // Отступ сверху

  // label_subscribers = lv_label_create(views_group);
  // lv_label_set_text(label_subscribers, "0");
  // lv_obj_set_width(label_subscribers, LV_PCT(100));
  // lv_obj_set_style_text_align(label_subscribers, LV_TEXT_ALIGN_CENTER, 0);
  // lv_obj_set_style_text_color(label_subscribers, lv_color_white(), 0);
  // lv_obj_set_style_text_font(label_subscribers, &lv_font_montserrat_38, 0);
}

// void draw_youtube_logo(lv_obj_t *canvas, int cx, int cy, int w) {
//   int h = (int)(w * 0.7f);
//   int r = (int)(h * 0.25f);
//   int x = cx - w / 2;
//   int y = cy - h / 2;

//   lv_draw_rect_dsc_t rect_dsc;
//   lv_draw_rect_dsc_init(&rect_dsc);
//   rect_dsc.bg_color = lv_color_make(255, 0, 0);
//   rect_dsc.radius = r;
//   rect_dsc.border_width = 0;
//   lv_canvas_draw_rect(canvas, x, y, w, h, &rect_dsc);

//   int tw = (int)(w * 0.36f);
//   int th = (int)(h * 0.42f);

//   lv_point_t pts[3] = {
//     { (lv_coord_t)(cx - tw / 2), (lv_coord_t)(cy - th / 2) },
//     { (lv_coord_t)(cx - tw / 2), (lv_coord_t)(cy + th / 2) },
//     { (lv_coord_t)(cx + tw / 2), (lv_coord_t)(cy) },
//   };

//   lv_draw_rect_dsc_t poly_dsc;
//   lv_draw_rect_dsc_init(&poly_dsc);
//   poly_dsc.bg_color = lv_color_white();
//   poly_dsc.border_width = 0;
//   lv_canvas_draw_polygon(canvas, pts, 3, &poly_dsc);
// }

void setup() {
  LOG_BEGIN(BAUD);
  delay(200);
  LCD_Init();
  Lvgl_Init();
  VSerial.begin(BAUD, SERIAL_8N1, VESC_RX_PIN, VESC_TX_PIN);
  UART.setSerialPort(&VSerial);
  createUI();

  // initWifi();
  // initBLE();
  delay(200);
}

void loop() {
  Timer_Loop();
  // if (millis() - lastFetch >= REFRESH_MS) {
  //   lastFetch = millis();
  //   if (WiFi.status() != WL_CONNECTED) {
  //     initWifi();
  //   }
  //   uint64_t subs = 0, views = 0;
  //   bool ok = (WiFi.status() == WL_CONNECTED) && fetchStats(subs, views);
  //   char buf[16];
  //   snprintf(buf, sizeof(buf), "%s", formatK(views).c_str());
  //   lv_label_set_text(label_views, buf);

  //   snprintf(buf, sizeof(buf), "%s", formatK(subs).c_str());
  //   lv_label_set_text(label_subscribers, buf);
  //   LOG_PRINTF("Update: subs=%llu views=%llu ok=%d\n", subs, views, ok);
  // }

  if (UART.getVescValues()) {
    float voltage = UART.data.inpVoltage;
    char buf[16];
    snprintf(buf, sizeof(buf), "%.2f V", voltage);
    lv_label_set_text(label_subscribers, buf);
  } else {
    lv_label_set_text(label_subscribers, "No Data");
  }
  lastFetch = millis();

  char buf[32];
  snprintf(buf, sizeof(buf), "%lu ms", lastFetch);
  LOG_PRINTLN(buf);
  lv_label_set_text(label_views, buf);


  delay(500);
}
