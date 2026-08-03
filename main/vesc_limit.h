#pragma once

#include <Arduino.h>

// Temporary limits in the VESC motor config (COMM_SET_MCCONF_TEMP packet).
//
// Why not setRPM/setBrakeCurrent: the throttle app (ADC/PPM) inside the VESC rewrites
// the setpoint every control cycle (~1 kHz), so it simply overwrites the rare UART
// commands - neither the speed limit nor the lock can be built that way. Instead the
// motor config itself is patched: the controller clamps the current, and the throttle
// physically cannot exceed it.
//
// store = 0 - nothing is written to flash, the values live until the VESC reboots (and
// loop() re-sends them periodically so the limit survives a controller reboot).
//
// IMPORTANT: the packet rewrites all eight fields at once, so neither the speed limit
// nor the lock may send it on its own - both regulators are folded into one struct and
// loop() sends that as a whole.
struct VescLimits {
  float min_erpm;           // l_min_erpm - reverse
  float max_erpm;           // l_max_erpm - speed limit
  float current_min_scale;  // 0..1, braking side (0 would also kill our setBrakeCurrent)
  float current_max_scale;  // 0..1, drive side: 0 = the throttle produces no current (lock)
};

void vesc_send_limits(Stream &port, const VescLimits &lim);
