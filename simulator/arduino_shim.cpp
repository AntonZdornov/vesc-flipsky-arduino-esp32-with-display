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

float kmh_to_erpm(float kmh) {
    const float WHEEL_D_INCH = 10.0f;
    const int   POLE_PAIRS = 14;
    const float v_mps = kmh * 1000.0f / 3600.0f;
    const float diameter_m = WHEEL_D_INCH * 0.0254f;
    const float circ_m = 3.14159265f * diameter_m;
    const float rpm_mech = (v_mps / circ_m) * 60.0f;
    return rpm_mech * POLE_PAIRS;
}
