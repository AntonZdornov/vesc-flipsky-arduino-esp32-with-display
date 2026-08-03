/* Headless render of the UI to BMP: the same ui_build() as in the SDL simulator but
   without a window - handy to inspect each tab's layout (in CI and over SSH).
   Build: cmake --build . --target vesc_ui_shot; run: ./vesc_ui_shot <directory> */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <lvgl.h>
#include "ui.h"

#define SHOT_W 240
#define SHOT_H 280

static lv_color_t fb[SHOT_W * SHOT_H];
static lv_color_t draw_buf_px[SHOT_W * SHOT_H];
static lv_disp_draw_buf_t draw_buf;

static void shot_flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *px) {
    for (int y = area->y1; y <= area->y2; y++) {
        for (int x = area->x1; x <= area->x2; x++) {
            fb[y * SHOT_W + x] = *px++;
        }
    }
    lv_disp_flush_ready(drv);
}

static void write_bmp(const char *path) {
    const int row = SHOT_W * 3;
    const int pad = (4 - (row % 4)) % 4;
    const int data = (row + pad) * SHOT_H;
    unsigned char hdr[54] = { 0 };
    const int total = 54 + data;
    hdr[0] = 'B'; hdr[1] = 'M';
    hdr[2] = total & 0xFF; hdr[3] = (total >> 8) & 0xFF;
    hdr[4] = (total >> 16) & 0xFF; hdr[5] = (total >> 24) & 0xFF;
    hdr[10] = 54;
    hdr[14] = 40;
    hdr[18] = SHOT_W & 0xFF; hdr[19] = (SHOT_W >> 8) & 0xFF;
    hdr[22] = SHOT_H & 0xFF; hdr[23] = (SHOT_H >> 8) & 0xFF;
    hdr[26] = 1;
    hdr[28] = 24;
    hdr[34] = data & 0xFF; hdr[35] = (data >> 8) & 0xFF;
    hdr[36] = (data >> 16) & 0xFF; hdr[37] = (data >> 24) & 0xFF;

    FILE *f = fopen(path, "wb");
    if (!f) { printf("cannot write %s\n", path); return; }
    fwrite(hdr, 1, sizeof(hdr), f);
    const unsigned char zero[3] = { 0, 0, 0 };
    for (int y = SHOT_H - 1; y >= 0; y--) {   /* BMP stores rows bottom-up */
        for (int x = 0; x < SHOT_W; x++) {
            const uint32_t c = lv_color_to32(fb[y * SHOT_W + x]);
            const unsigned char bgr[3] = { c & 0xFF, (c >> 8) & 0xFF, (c >> 16) & 0xFF };
            fwrite(bgr, 1, 3, f);
        }
        if (pad) fwrite(zero, 1, pad, f);
    }
    fclose(f);
    printf("%s\n", path);
}

/* Scripted "finger": clicks at coordinates, to drive the lock keypad and the UI
   rebuild without a window. */
static lv_point_t touch_pt;
static bool touch_down = false;

static void touch_read(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    (void)drv;
    data->point = touch_pt;
    data->state = touch_down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static void pump(int ms);

static void click_at(lv_coord_t x, lv_coord_t y) {
    touch_pt.x = x;
    touch_pt.y = y;
    touch_down = true;
    pump(120);
    touch_down = false;
    pump(200);
}

static void click_obj(lv_obj_t *obj) {
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    click_at((a.x1 + a.x2) / 2, (a.y1 + a.y2) / 2);
}

static void pump(int ms) {
    for (int i = 0; i < ms; i += 5) {
        lv_timer_handler();
        lv_tick_inc(5);
    }
    lv_refr_now(NULL);
}

int main(int argc, char *argv[]) {
    const char *dir = argc > 1 ? argv[1] : ".";

    lv_init();
    lv_disp_draw_buf_init(&draw_buf, draw_buf_px, NULL, SHOT_W * SHOT_H);
    static lv_disp_drv_t drv;
    lv_disp_drv_init(&drv);
    drv.draw_buf = &draw_buf;
    drv.flush_cb = shot_flush;
    drv.hor_res = SHOT_W;
    drv.ver_res = SHOT_H;
    lv_disp_drv_register(&drv);

    ui_set_debug_enabled(1);   /* so the diagnostics tab is rendered too */
    ui_build();

    /* Plausible data */
    ui_set_battery(51.2f);
    ui_set_temp(38.4f);
    ui_set_speed(7000.0f);
    ui_set_power(1850.0f);
    ui_set_currents(32.0f, 24.0f);
    ui_set_tachometer(120000.0f);
    ui_set_tachometerAbs(4200000.0f);
    ui_set_cost(12.3f);
    ui_set_diag(6 /* OVER_TEMP_MOTOR */, 0.42f, 61.5f, 1);
    ui_set_lock(1);

    /* tabview is the screen's first child (created before the indicator dots) */
    lv_obj_t *tv = lv_obj_get_child(lv_scr_act(), 0);
    const char *names[] = { "0-current", "1-limit", "2-dash", "3-trip", "4-lock", "5-diag" };
    char path[512];
    for (int i = 0; i < 6; i++) {
        lv_tabview_set_act(tv, i, LV_ANIM_OFF);
        pump(400);
        snprintf(path, sizeof(path), "%s/tab-%s.bmp", dir, names[i]);
        write_bmp(path);
    }

    /* Settings menu: the overlay is the top layer's first child */
    lv_tabview_set_act(tv, 2, LV_ANIM_OFF);
    lv_obj_clear_flag(lv_obj_get_child(lv_layer_top(), 0), LV_OBJ_FLAG_HIDDEN);
    pump(400);
    snprintf(path, sizeof(path), "%s/overlay-settings.bmp", dir);
    write_bmp(path);
    lv_obj_add_flag(lv_obj_get_child(lv_layer_top(), 0), LV_OBJ_FLAG_HIDDEN);

    /* ===== Scenario: enter the code and toggle diagnostics ===== */
    static lv_indev_drv_t indev;
    lv_indev_drv_init(&indev);
    indev.type = LV_INDEV_TYPE_POINTER;
    indev.read_cb = touch_read;
    lv_indev_drv_register(&indev);

    lv_obj_t *lock_page = lv_obj_get_child(lv_tabview_get_content(tv), 4);
    lv_obj_t *keypad = lv_obj_get_child(lock_page, 2);
    lv_tabview_set_act(tv, 4, LV_ANIM_OFF);
    pump(400);

    lv_area_t k;
    lv_obj_get_coords(keypad, &k);
    const lv_coord_t kw = lv_area_get_width(&k) / 3;
    const lv_coord_t kh = lv_area_get_height(&k) / 3;
    const char *code = "1987";
    printf("lock before: %d\n", (int)ui_get_lock());
    for (const char *c = code; *c; c++) {
        const int d = *c - '1';
        click_at(k.x1 + (d % 3) * kw + kw / 2, k.y1 + (d / 3) * kh + kh / 2);
    }
    printf("lock after 1987: %d (expected: toggled)\n", (int)ui_get_lock());
    snprintf(path, sizeof(path), "%s/after-code.bmp", dir);
    write_bmp(path);

    /* Gear button on the report tab -> diagnostics toggle -> UI rebuild */
    lv_tabview_set_act(tv, 3, LV_ANIM_OFF);
    pump(400);
    lv_obj_t *trip = lv_obj_get_child(lv_tabview_get_content(tv), 3);
    lv_obj_t *gear = NULL;                 /* found by size: it is the only 34x34 object */
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(trip); i++) {
        lv_obj_t *ch = lv_obj_get_child(trip, i);
        if (lv_obj_get_width(ch) == 34 && lv_obj_get_height(ch) == 34) gear = ch;
    }
    if (!gear) { printf("ERROR: gear button not found\n"); return 1; }
    click_obj(gear);
    lv_obj_t *panel = lv_obj_get_child(lv_obj_get_child(lv_layer_top(), 0), 0);
    click_obj(lv_obj_get_child(panel, 1)); /* DEBUG MODE */
    pump(600);                             /* the rebuild lv_async runs here */
    printf("debug after toggle: %d (expected: 0)\n", (int)ui_get_debug_enabled());
    printf("lock survived rebuild: %d\n", (int)ui_get_lock());
    snprintf(path, sizeof(path), "%s/after-rebuild.bmp", dir);
    write_bmp(path);
    return 0;
}
