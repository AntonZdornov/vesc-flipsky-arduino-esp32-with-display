#include <Arduino.h>
#include <HardwareSerial.h>
#include <VescUart.h>
#include "display.h"
#include "lvgl_driver.h"
// #include "wifi_service.h"
// #include "ble_service.h"
#include "logger.h"
#include "utils.h"
// #include <WiFi.h>
#include "ui_globals.h"
// #include <WiFiClientSecure.h>
// #include <HTTPClient.h>
// #include <ArduinoJson.h>
#include <lvgl.h>

// WIFISettings
// WiFiClient client;

const char *YT_API_KEY = "";
const char *YT_CHANNEL_ID = "";
const unsigned long REFRESH_MS = 60000;
unsigned long lastFetch = 0;

// UI
#define SRC_W 250
#define SRC_H 250
static lv_color_t src_buf[SRC_W * SRC_H];
lv_obj_t *root_container;
lv_obj_t *label_views = nullptr;
lv_obj_t *label_subscribers = nullptr;
extern const lv_font_t lv_font_montserrat_48;
extern const lv_font_t lv_font_montserrat_38;
extern const lv_font_t lv_font_montserrat_12;
extern const lv_font_t lv_font_montserrat_22;

// bool fetchStats(uint64_t &subs, uint64_t &views) {
//   WiFiClientSecure client;
//   client.setTimeout(15000);
//   // Для простоты: отключаем проверку сертификата. В проде лучше задать корневой сертификат.
//   client.setInsecure();

//   HTTPClient https;
//   String url = String("https://www.googleapis.com/youtube/v3/channels")
//                + "?part=statistics&id=" + YT_CHANNEL_ID + "&key=" + YT_API_KEY;

//   if (!https.begin(client, url)) {
//     LOG_PRINTLN("HTTPS begin() failed");
//     return false;
//   }

//   int code = https.GET();
//   if (code != HTTP_CODE_OK) {
//     LOG_PRINTF("HTTP error: %d\n", code);
//     https.end();
//     return false;
//   }

//   // 1) читаем весь ответ в строку
//   String payload = https.getString();
//   https.end();

//   // 2) выводим для отладки
//   // LOG_PRINTLN(payload);

//   // 3) на всякий случай подчистим и парсим из строки (НЕ из потока!)
//   payload.trim();
//   int brace = payload.indexOf('{');  // если вдруг есть мусор до JSON
//   if (brace > 0) payload = payload.substring(brace);

//   DynamicJsonDocument doc(4096);  // буфер побольше
//   DeserializationError err = deserializeJson(doc, payload);

//   // // Парсим JSON
//   // DynamicJsonDocument doc(8192);  // хватает для statistics
//   // DeserializationError err = deserializeJson(doc, https.getStream());
//   https.end();
//   if (err) {
//     LOG_PRINTF("JSON error: %s\n", err.c_str());
//     return false;
//   }

//   // Путь: items[0].statistics.subscriberCount / viewCount
//   if (!doc["items"] || doc["items"].size() == 0) {
//     LOG_PRINTLN("No items in response");
//     return false;
//   }

//   JsonObject stats = doc["items"][0]["statistics"];
//   const char *subStr = stats["subscriberCount"] | "0";
//   const char *viewStr = stats["viewCount"] | "0";

//   // 64-бит, потому что просмотры бывают очень большие
//   subs = strtoull(subStr, nullptr, 10);
//   views = strtoull(viewStr, nullptr, 10);

//   return true;
// }

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

  make_logo_img(root_container, 0, 0.5f, /*dst center*/ 60, 60);

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

  lv_obj_t *label_subscribers_title = lv_label_create(views_group);
  lv_label_set_text(label_subscribers_title, "Subscribers");
  lv_obj_set_width(label_subscribers_title, LV_PCT(100));
  lv_obj_set_style_text_align(label_subscribers_title, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(label_subscribers_title, lv_color_white(), 0);
  lv_obj_set_style_text_font(label_subscribers_title, &lv_font_montserrat_22, 0);
  lv_obj_set_style_pad_top(label_subscribers_title, 5, LV_PART_MAIN);  // Отступ сверху

  label_subscribers = lv_label_create(views_group);
  lv_label_set_text(label_subscribers, "0");
  lv_obj_set_width(label_subscribers, LV_PCT(100));
  lv_obj_set_style_text_align(label_subscribers, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(label_subscribers, lv_color_white(), 0);
  lv_obj_set_style_text_font(label_subscribers, &lv_font_montserrat_38, 0);
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

void make_logo_img(lv_obj_t *parent, int angle_deg, float zoom_factor, lv_coord_t x, lv_coord_t y) {
  // 1) Канва-источник
  lv_obj_t *src = lv_canvas_create(parent);
  lv_canvas_set_buffer(src, src_buf, SRC_W, SRC_H, LV_IMG_CF_TRUE_COLOR);
  lv_canvas_fill_bg(src, lv_color_black(), LV_OPA_TRANSP);  // прозрачный фон
  lv_obj_add_flag(src, LV_OBJ_FLAG_HIDDEN);                 // не показываем сам source

  // Рисуем логотип по центру источника (ширина 160)
  // draw_youtube_logo(src, SRC_W / 2, SRC_H / 2, 60);

  // 2) Делаем img-дескриптор, который указывает на память канвы
  static lv_img_dsc_t src_dsc;  // static, чтобы не исчез
  src_dsc.header.always_zero = 0;
  src_dsc.header.w = SRC_W;
  src_dsc.header.h = SRC_H;
  src_dsc.data_size = SRC_W * SRC_H * sizeof(lv_color_t);
  src_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
  src_dsc.data = (const uint8_t *)src_buf;

  // 3) Создаём lv_img, задаём источник, угол и зум
  lv_obj_t *img = lv_img_create(parent);
  lv_img_set_src(img, &src_dsc);

  // угол в десятых долях градуса: 90° => 900, 180° => 1800
  lv_img_set_angle(img, angle_deg * 10);

  // масштаб: 256 = 1.0x, 128 = 0.5x, 512 = 2.0x
  uint16_t zoom = (uint16_t)(zoom_factor * 512.0f);
  lv_img_set_zoom(img, zoom);

  // Поворот/масштаб вокруг центра
  lv_img_set_pivot(img, SRC_W / 2, SRC_H / 2);

  // Позиция на экране
  lv_obj_set_pos(img, x - SRC_W / 2, y - SRC_H / 2);
}

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
