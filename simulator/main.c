#include <stdio.h>
#include <SDL2/SDL.h>
#include <lvgl.h>
#include "sdl/sdl.h"
#include "ui.h"

#define BUF_LINES 100

static lv_color_t buf1[SDL_HOR_RES * BUF_LINES];
static lv_color_t buf2[SDL_HOR_RES * BUF_LINES];
static lv_disp_draw_buf_t disp_buf;

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;

    lv_init();
    sdl_init();

    lv_disp_draw_buf_init(&disp_buf, buf1, buf2, SDL_HOR_RES * BUF_LINES);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.draw_buf = &disp_buf;
    disp_drv.flush_cb = sdl_display_flush;
    disp_drv.hor_res  = SDL_HOR_RES;
    disp_drv.ver_res  = SDL_VER_RES;
    lv_disp_drv_register(&disp_drv);

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = sdl_mouse_read;
    lv_indev_drv_register(&indev_drv);

    ui_build();

    /* Fake VESC data driver: ramps speed, drains battery, sweeps temp,
       monotonically increments tachometers. */
    float voltage    = 54.6f;
    float speed_erpm = 0.0f;
    float temp       = 25.0f;
    float tacho      = 0.0f;
    float tacho_abs  = 0.0f;
    float cost       = 5.50f;
    int   dir        = 1;
    int   slow       = 0;

    while (1) {
        lv_timer_handler();
        SDL_Delay(5);
        lv_tick_inc(5);

        if (++slow >= 10) { /* every ~50ms */
            slow = 0;
            speed_erpm += dir * 200.0f;
            if (speed_erpm > 9000.0f) dir = -1;
            if (speed_erpm < 0.0f)    { speed_erpm = 0.0f; dir = 1; }

            voltage -= 0.001f;
            if (voltage < 39.0f) voltage = 54.6f;

            temp += 0.05f;
            if (temp > 45.0f) temp = 25.0f;

            tacho     += speed_erpm * 0.001f;
            tacho_abs += speed_erpm * 0.001f;
            cost      += 0.002f;

            ui_set_battery(voltage);
            ui_set_speed(speed_erpm);
            ui_set_temp(temp);
            ui_set_tachometer(tacho);
            ui_set_tachometerAbs(tacho_abs);
            ui_set_cost(cost);
        }
    }
    return 0;
}
