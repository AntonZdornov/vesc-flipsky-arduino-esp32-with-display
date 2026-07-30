#pragma once

#include <lvgl.h>
#include <lv_conf.h>
#include <demos/lv_demos.h>
#include <esp_heap_caps.h>
#include "display.h"

// Разрешение берём из display.h (ESP32-S3-Touch-LCD-1.69: 240x280 в портрете)
#define LVGL_WIDTH    LCD_WIDTH
#define LVGL_HEIGHT   LCD_HEIGHT
#define LVGL_BUF_LEN  (LVGL_WIDTH * LVGL_HEIGHT / 20)

#define EXAMPLE_LVGL_TICK_PERIOD_MS  5


void Lvgl_print(const char * buf);
void Lvgl_Display_LCD( lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p ); // Displays LVGL content on the LCD.    This function implements associating LVGL data to the LCD screen
void Lvgl_Touchpad_Read( lv_indev_drv_t * indev_drv, lv_indev_data_t * data ); // Read the touchpad
void example_increase_lvgl_tick(void *arg);

void Lvgl_Init(void);

// Отдельная задача FreeRTOS, которая крутит lv_timer_handler():
// рендер и обработка свайпов не зависят от опроса VESC в loop().
void Lvgl_Start_Task(void);

// LVGL не потокобезопасен: любые lv_*/ui_set_* вызовы ВНЕ задачи LVGL
// (то есть из loop()) нужно обернуть в Lvgl_Lock()/Lvgl_Unlock().
// timeout_ms < 0 — ждать бесконечно. Возвращает false, если не дождались.
bool Lvgl_Lock(int timeout_ms);
void Lvgl_Unlock(void);

// Оставлено для совместимости: если задача LVGL запущена, вызывать не нужно.
void Timer_Loop(void);
