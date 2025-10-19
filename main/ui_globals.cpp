#include <lvgl.h>
#include "ui_globals.h"

void log_message(const char* msg) {
  if (logs_label) {
    lv_label_set_text(logs_label, msg);
  }
}