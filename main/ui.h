#pragma once
#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

void ui_build(void);
void ui_set_battery(float volts);
void ui_set_speed(float rpm);
void ui_set_temp(float celsius);
void ui_set_tachometer(float tachometer);
void ui_set_tachometerAbs(float tachometer);
void ui_set_cost(float ils);
// Currents on the report tab: motor phase current and battery input current (A).
// The values are smoothed internally - loop() may pass raw ones.
void ui_set_currents(float motor_a, float input_a);
// Wheel power (W = voltage x input current): the blue ring around the speedometer
// and the kW label below it. Smoothed internally.
void ui_set_power(float watts);

// 25 km/h speed limit (left tab). Always off after power-up - the state is not
// persisted anywhere.
bool ui_get_limit25(void);
void ui_set_limit25(bool on);

// Lock (keypad tab). Unlike the 25 km/h limit, this state MUST survive a power
// cycle - loop() stores it in NVS and restores it at boot via ui_set_lock().
bool ui_get_lock(void);
void ui_set_lock(bool on);

// Diagnostics mode (settings menu behind the gear button). Adds the last tab with
// the controller faults. Call ui_set_debug_enabled() BEFORE ui_build(): the set of
// tabs is fixed at the moment the UI is built.
bool ui_get_debug_enabled(void);
void ui_set_debug_enabled(bool on);

// Data for the diagnostics tab. fault_code is mc_fault_code from the VESC telemetry
// (an int, not the enum: ui.cpp is also compiled by the PC simulator, which has no
// datatypes.h). comm_ok == false means there was no reply from the VESC.
void ui_set_diag(int fault_code, float duty, float temp_motor, bool comm_ok);

// Motor current from the leftmost tab: the value confirmed with the UPDATE button.
// loop() must repeat it to the VESC every cycle - otherwise the command decays on the
// controller's timeout (~1 s). 0 = send nothing.
float ui_get_current_applied(void);

// Last speed shown on the speedometer, km/h (smoothed)
float ui_speed_kmh(void);
// km/h -> eRPM using the same constants as the speedometer
float ui_kmh_to_erpm(float kmh);

#ifdef __cplusplus
}
#endif
