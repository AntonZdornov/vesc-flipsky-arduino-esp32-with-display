/*****************************************************************************
  | File        :   LVGL_Driver.c

  | help        :
    The provided LVGL library file must be installed first
******************************************************************************/
#include "lvgl_driver.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "touch.h"

static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf1[LVGL_BUF_LEN];
static lv_color_t buf2[LVGL_BUF_LEN];

// Recursive mutex: LVGL runs in its own task while ui_set_* is called from loop()
static SemaphoreHandle_t lvgl_mutex = NULL;

#define LVGL_TASK_STACK      12288
#define LVGL_TASK_PRIORITY   2  // above loopTask (1) so the UI does not wait on VESC polling
#define LVGL_TASK_CORE       0  // loopTask lives on core 1 - keep them on separate cores
#define LVGL_TASK_MIN_MS     5
#define LVGL_TASK_MAX_MS     20


/*  Display flushing
    Displays LVGL content on the LCD
    This function implements associating LVGL data to the LCD screen
*/
void Lvgl_Display_LCD(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p) {
  LCD_addWindow(area->x1, area->y1, area->x2, area->y2, (uint16_t *)&color_p->full);
  lv_disp_flush_ready(disp_drv);
}
/*Read the touchpad*/
void Lvgl_Touchpad_Read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data) {
  uint16_t x = 0, y = 0;
  if (Touch_Read(&x, &y)) {
    data->point.x = x;
    data->point.y = y;
    data->state = LV_INDEV_STATE_PRESSED;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}
void example_increase_lvgl_tick(void *arg) {
  /* Tell LVGL how many milliseconds has elapsed */
  lv_tick_inc(EXAMPLE_LVGL_TICK_PERIOD_MS);
}

bool Lvgl_Lock(int timeout_ms) {
  if (lvgl_mutex == NULL) return true;  // task not started yet - no contention
  const TickType_t ticks = (timeout_ms < 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
  return xSemaphoreTakeRecursive(lvgl_mutex, ticks) == pdTRUE;
}

void Lvgl_Unlock(void) {
  if (lvgl_mutex == NULL) return;
  xSemaphoreGiveRecursive(lvgl_mutex);
}

static void lvgl_task(void *arg) {
  for (;;) {
    uint32_t next_ms = LVGL_TASK_MAX_MS;
    if (Lvgl_Lock(-1)) {
      next_ms = lv_timer_handler();  // it tells us when to call it again
      Lvgl_Unlock();
    }
    if (next_ms < LVGL_TASK_MIN_MS) next_ms = LVGL_TASK_MIN_MS;
    if (next_ms > LVGL_TASK_MAX_MS) next_ms = LVGL_TASK_MAX_MS;  // keeps swipes responsive
    vTaskDelay(pdMS_TO_TICKS(next_ms));
  }
}

void Lvgl_Start_Task(void) {
  if (lvgl_mutex != NULL) return;
  lvgl_mutex = xSemaphoreCreateRecursiveMutex();
  xTaskCreatePinnedToCore(lvgl_task, "lvgl", LVGL_TASK_STACK, NULL,
                          LVGL_TASK_PRIORITY, NULL, LVGL_TASK_CORE);
}

void Lvgl_Init(void) {
  lv_init();
  // lv_obj_clean(lv_scr_act());

  lv_disp_draw_buf_init(&draw_buf, buf1, buf2, LVGL_BUF_LEN);

  /*Initialize the display*/
  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  /*Change the following line to your display resolution*/
  disp_drv.hor_res = LVGL_WIDTH;
  disp_drv.ver_res = LVGL_HEIGHT;
  disp_drv.flush_cb = Lvgl_Display_LCD;
  // full_refresh = 0: redraw only the changed areas. A full frame is
  // 240x280x2 = 134 KB over SPI at 40 MHz - about 30 ms, noticeable while swiping.
  // (besides, full_refresh expects a full-screen buffer and ours is 1/20 of that).
  disp_drv.full_refresh = 0;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);


  // Optional: rotate via LVGL API if needed
  // lv_disp_t *disp = lv_disp_get_default();
  // lv_disp_set_rotation(disp, LV_DISP_ROT_90);

  /*Initialize the input device driver (CST816T capacitive touch)*/
  Touch_Init();
  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = Lvgl_Touchpad_Read;
  lv_indev_drv_register(&indev_drv);

  /* Create simple label */
  // lv_obj_t *label = lv_label_create( lv_scr_act() );
  // lv_label_set_text( label, "Hello!");
  // lv_obj_align( label, LV_ALIGN_CENTER, 0, 0 );

  const esp_timer_create_args_t lvgl_tick_timer_args = {
    .callback = &example_increase_lvgl_tick,
    .name = "lvgl_tick"
  };
  esp_timer_handle_t lvgl_tick_timer = NULL;
  esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer);
  esp_timer_start_periodic(lvgl_tick_timer, EXAMPLE_LVGL_TICK_PERIOD_MS * 1000);

  // lv_disp_t *disp = lv_disp_get_default();
  // lv_disp_set_rotation(disp, LV_DISP_ROT_90);
}
