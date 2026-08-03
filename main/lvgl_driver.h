#pragma once

#include <lvgl.h>
#include <lv_conf.h>
#include "display.h"

// Resolution comes from display.h (ESP32-S3-Touch-LCD-1.69: 240x280 in portrait)
#define LVGL_WIDTH    LCD_WIDTH
#define LVGL_HEIGHT   LCD_HEIGHT
#define LVGL_BUF_LEN  (LVGL_WIDTH * LVGL_HEIGHT / 20)

#define EXAMPLE_LVGL_TICK_PERIOD_MS  5


void Lvgl_Display_LCD( lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p ); // Displays LVGL content on the LCD.    This function implements associating LVGL data to the LCD screen
void Lvgl_Touchpad_Read( lv_indev_drv_t * indev_drv, lv_indev_data_t * data ); // Read the touchpad
void example_increase_lvgl_tick(void *arg);

void Lvgl_Init(void);

// A dedicated FreeRTOS task running lv_timer_handler(): rendering and swipe
// handling do not depend on the VESC polling done in loop().
void Lvgl_Start_Task(void);

// LVGL is not thread-safe: every lv_*/ui_set_* call made OUTSIDE the LVGL task
// (i.e. from loop()) must be wrapped in Lvgl_Lock()/Lvgl_Unlock().
// timeout_ms < 0 means wait forever. Returns false if the lock was not acquired.
bool Lvgl_Lock(int timeout_ms);
void Lvgl_Unlock(void);
