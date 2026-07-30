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
// static lv_color_t* buf1 = (lv_color_t*) heap_caps_malloc(LVGL_BUF_LEN, MALLOC_CAP_SPIRAM);
// static lv_color_t* buf2 = (lv_color_t*) heap_caps_malloc(LVGL_BUF_LEN,f MALLOC_CAP_SPIRAM);

// Рекурсивный мьютекс: LVGL крутится в своей задаче, а ui_set_* зовутся из loop()
static SemaphoreHandle_t lvgl_mutex = NULL;

#define LVGL_TASK_STACK      12288
#define LVGL_TASK_PRIORITY   2  // выше loopTask (1), чтобы UI не ждал опроса VESC
#define LVGL_TASK_CORE       0  // loopTask живёт на core 1 — разводим по ядрам
#define LVGL_TASK_MIN_MS     5
#define LVGL_TASK_MAX_MS     20


/* Serial debugging */
void Lvgl_print(const char *buf) {
  // Serial.printf(buf);
  // Serial.flush();
}

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
  if (lvgl_mutex == NULL) return true;  // задача ещё не запущена — конкуренции нет
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
      next_ms = lv_timer_handler();  // сам говорит, через сколько его позвать
      Lvgl_Unlock();
    }
    if (next_ms < LVGL_TASK_MIN_MS) next_ms = LVGL_TASK_MIN_MS;
    if (next_ms > LVGL_TASK_MAX_MS) next_ms = LVGL_TASK_MAX_MS;  // отклик на свайпы
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
  // full_refresh = 0: перерисовываем только изменившиеся области. Полный кадр
  // 240x280x2 = 134 КБ по SPI на 40 МГц — это ~30 мс, на свайпах заметно.
  // (к тому же full_refresh рассчитан на буфер размером во весь экран, а у нас 1/20).
  disp_drv.full_refresh = 0;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);


  // Optional: rotate via LVGL API if needed
  // lv_disp_t *disp = lv_disp_get_default();
  // lv_disp_set_rotation(disp, LV_DISP_ROT_90);

  /*Initialize the input device driver (ёмкостный тач CST816T)*/
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
void Timer_Loop(void) {
  lv_timer_handler(); /* let the GUI do its work */
  // delay( 5 );
}
