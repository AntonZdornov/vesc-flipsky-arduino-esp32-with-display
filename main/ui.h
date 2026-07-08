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

#ifdef __cplusplus
}
#endif
