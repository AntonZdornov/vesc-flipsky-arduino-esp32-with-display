#include <Arduino.h>
#include "utils.h"

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