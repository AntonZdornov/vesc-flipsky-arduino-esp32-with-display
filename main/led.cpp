#include <Adafruit_NeoPixel.h>
#include "led.h"

/// ВНИМАНИЕ: на Waveshare ESP32-S3-Touch-LCD-1.69 встроенной адресной RGB-ленты
/// НЕТ (GPIO8 здесь занят под LCD_RST!). Пин ниже — под внешний WS2812B,
/// подключённый к свободному паду GPIO17. Код всё ещё не вызывается из setup().
static const uint8_t LED_PIN = 17;
static const uint8_t NUM_LEDS = 1;

// RGB_BUILTIN определён не во всех вариантах плат ESP32-S3 (в частности,
// у ESP32-S3-Touch-LCD-1.69 встроенного LED нет) — иначе сборка падает.
#ifndef RGB_BUILTIN
#define RGB_BUILTIN LED_PIN
#endif

static bool ledsInited = false;
static Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

void leds_init(uint8_t brightness = 120) {
  if (ledsInited) return;
  strip.begin();
  strip.setBrightness(brightness);
  strip.clear();
  strip.show();
  pinMode(RGB_BUILTIN, OUTPUT);
  digitalWrite(RGB_BUILTIN, LOW);  // начальное состояние (LOW=выкл для многих плат)
  ledsInited = true;
}

void led_on(uint8_t r, uint8_t g, uint8_t b) {
  leds_init();                     // на случай, если не вызвали заранее
  for (uint16_t i = 0; i < NUM_LEDS; i++) {
    strip.setPixelColor(i, strip.Color(r, g, b));
  }
  strip.show();

  // если есть однопиновый встроенный LED:
  digitalWrite(RGB_BUILTIN, HIGH); // чаще HIGH = вкл (проверь на своей плате)
}

void led_off() {
  leds_init();
  strip.clear();
  strip.show();

  digitalWrite(RGB_BUILTIN, LOW);  // чаще LOW = выкл
}