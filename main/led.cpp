#include <Adafruit_NeoPixel.h>
#include "led.h"

/// На большинстве отображающих плат на ESP32‑C6 встроена адресная RGB‑лента:
/// GPIO8 = управляющий пин для WS2812B.
static const uint8_t LED_PIN = 8;
static const uint8_t NUM_LEDS = 1;
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