#include <Arduino.h>
#include "utils.h"

uint8_t calcBatteryPercent(float voltage) {
  const float full = 54.6;   // 100%
  const float empty = 39.0;  // 0%
  if (voltage >= full) return 100;
  if (voltage <= empty) return 0;
  // linear interpolation
  return (uint8_t)(((voltage - empty) / (full - empty)) * 100.0f);
}

float erpm_to_kmh(long erpm, int pole_pairs, float wheel_circumference_m) {
  float mech_rpm = erpm / (float)pole_pairs;
  float kmh = mech_rpm * wheel_circumference_m * 60.0f / 1000.0f;
  return fabs(kmh);  // absolute value, so it never goes negative
}
