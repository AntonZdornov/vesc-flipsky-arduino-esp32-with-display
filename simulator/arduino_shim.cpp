#include <math.h>
#include <stdint.h>

uint8_t calcBatteryPercent(float voltage) {
    const float full = 54.6f, empty = 39.0f;
    if (voltage >= full)  return 100;
    if (voltage <= empty) return 0;
    return (uint8_t)(((voltage - empty) / (full - empty)) * 100.0f);
}

float erpm_to_kmh(long erpm, int pole_pairs, float circ_m) {
    float mech_rpm = erpm / (float)pole_pairs;
    return fabsf(mech_rpm * circ_m * 60.0f / 1000.0f);
}
