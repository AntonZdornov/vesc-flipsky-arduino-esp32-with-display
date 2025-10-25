#include <Arduino.h>
#include "utils.h"

static const float WHEEL_D_INCH   = 10.0f;  // диаметр колеса (замени)
static const int   POLE_PAIRS     = 14;     // пары полюсов (магниты/2)
static const float GEAR_RATIO     = 1.0f;   // редуктор (если нет — 1.0)

String formatK(uint64_t n) {
  // компактный формат (12.3K / 4.5M), чтобы влезало на экран
  if (n >= 1000000ULL) {
    double m = n / 1000000.0;
    char buf[16];
    snprintf(buf, sizeof(buf), "%.1fM", m);
    return String(buf);
  } else if (n >= 1000ULL) {
    double k = n / 1000.0;
    char buf[16];
    snprintf(buf, sizeof(buf), "%.1fK", k);
    return String(buf);
  }
  return String((unsigned long)n);
}

uint8_t calcBatteryPercent(float voltage) {
  const float full = 54.6;   // 100%
  const float empty = 39.0;  // 0%
  if (voltage >= full) return 100;
  if (voltage <= empty) return 0;
  // линейная интерполяция
  return (uint8_t)(((voltage - empty) / (full - empty)) * 100.0f);
}

float erpm_to_kmh(long erpm,int pole_pairs,float wheel_circumference_m) {
  float mech_rpm = erpm / (float)pole_pairs;
  float kmh = mech_rpm * wheel_circumference_m * 60.0f / 1000.0f;
  return fabs(kmh);  // модуль, чтобы не было отрицательных значений
}

float kmh_to_erpm(float kmh) {
  const float v_mps = kmh * 1000.0f / 3600.0f;
  const float diameter_m = WHEEL_D_INCH * 0.0254f;
  const float circ_m = 3.14159265f * diameter_m;
  const float rpm_mech = (v_mps / circ_m) * 60.0f / GEAR_RATIO;
  return rpm_mech * POLE_PAIRS;
}