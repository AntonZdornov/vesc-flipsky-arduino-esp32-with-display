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
// Токи на вкладке отчёта: фазный ток мотора и входной ток от батареи (А).
// Значения сглаживаются внутри — из loop() можно передавать «сырые».
void ui_set_currents(float motor_a, float input_a);

// Ограничение скорости 25 км/ч (левая вкладка). После включения питания
// всегда выключено — состояние нигде не сохраняется.
bool ui_get_limit25(void);
void ui_set_limit25(bool on);

// Ток мотора с самой левой вкладки: значение, подтверждённое кнопкой UPDATE.
// loop() должен повторять его в VESC каждый цикл — иначе команда затухнет
// по таймауту контроллера (~1 с). 0 = ничего не шлём.
float ui_get_current_applied(void);

// Последняя показанная на спидометре скорость, км/ч (сглаженная)
float ui_speed_kmh(void);
// км/ч → eRPM теми же константами, что и спидометр
float ui_kmh_to_erpm(float kmh);

#ifdef __cplusplus
}
#endif
