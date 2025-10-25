#pragma once

String formatK(uint64_t n);
uint8_t calcBatteryPercent(float voltage);
float erpm_to_kmh(long erpm,int pole_pairs,float wheel_diameter);
float kmh_to_erpm(float kmh);