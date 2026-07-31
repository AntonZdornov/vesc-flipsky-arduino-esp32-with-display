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
// Мощность на колесо (Вт = напряжение × входной ток): синее кольцо вокруг
// спидометра и подпись в кВт под ним. Сглаживается внутри.
void ui_set_power(float watts);

// Ограничение скорости 25 км/ч (левая вкладка). После включения питания
// всегда выключено — состояние нигде не сохраняется.
bool ui_get_limit25(void);
void ui_set_limit25(bool on);

// Замок (вкладка с клавиатурой). В отличие от лимита 25, состояние ОБЯЗАНО
// пережить выключение питания — loop() сохраняет его в NVS, а на старте
// восстанавливает через ui_set_lock().
bool ui_get_lock(void);
void ui_set_lock(bool on);

// Режим диагностики (меню настроек по шестерёнке). Добавляет последнюю вкладку
// с ошибками контроллера. Вызывать ui_set_debug_enabled() ДО ui_build():
// набор вкладок фиксируется в момент сборки UI.
bool ui_get_debug_enabled(void);
void ui_set_debug_enabled(bool on);

// Данные для вкладки диагностики. fault_code — mc_fault_code из телеметрии VESC
// (int, а не enum: ui.cpp собирается и PC-симулятором, у которого нет datatypes.h).
// comm_ok == false — ответа от VESC не было.
void ui_set_diag(int fault_code, float duty, float temp_motor, bool comm_ok);

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
