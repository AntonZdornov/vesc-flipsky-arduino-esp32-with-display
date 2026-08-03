#pragma once

// Both helpers are also forward-declared in ui.cpp (which the PC simulator compiles
// too, where they come from simulator/arduino_shim.cpp instead).
uint8_t calcBatteryPercent(float voltage);
float erpm_to_kmh(long erpm, int pole_pairs, float wheel_circumference_m);
