#include <Arduino.h>
#include <HardwareSerial.h>
#include "VescUart.h"
#include "display.h"
#include "lvgl_driver.h"
#include "logger.h"
#include "utils.h"
#include "ui.h"
#include "ui_globals.h"
#include "vesc_limit.h"
#include <lvgl.h>
#include <Preferences.h>

Preferences prefs;

unsigned long lastFetch = 0;
static const unsigned long VESC_POLL_INTERVAL_MS = 200;  // как часто опрашиваем VESC

// Лимит скорости включается кнопкой на левой вкладке UI (ui_get_limit25()).
// После включения питания он всегда выключен — состояние не сохраняется.
static const float TARGET_KMH = 25.0f;  // лимит скорости
// Лимит реализован как временный Max ERPM в конфиге VESC (см. vesc_limit.h):
// setRPM по UART не работал, потому что приложение газа внутри VESC его затирает.
// ⚠️ ERPM «без лимита»: должен быть НЕ МЕНЬШЕ Max ERPM, выставленного в VESC Tool, —
// именно это значение уезжает в контроллер при выключении лимита (до его перезагрузки).
static const float NO_LIMIT_ERPM = 100000.0f;
static const unsigned long LIMIT_REFRESH_MS = 3000;  // повторяем, чтобы пережить ребут VESC
static bool limit_sent_state = false;                // какой режим уже отправлен в VESC
static bool lock_sent_state = false;                 // какой режим замка уже отправлен
static unsigned long last_limit_send_ms = 0;

// Замок (вкладка с клавиатурой, код в ui.cpp). Тяга отбирается внутри VESC —
// l_current_max_scale = 0, иначе приложение газа перебило бы любую нашу команду.
// setBrakeCurrent добавляется сверху как удерживающий тормоз и, как и setCurrent,
// требует повтора каждый цикл (таймаут команды ~1 с).
static const float LOCK_BRAKE_A = 6.0f;
static bool lock_saved_state = false;   // что уже лежит в NVS
static bool debug_saved_state = false;

// Плата: Waveshare ESP32-S3-Touch-LCD-1.69.
// GPIO0 — кнопка BOOT (единственная свободная кнопка на плате).
// GPIO2/GPIO3 — свободные пады гребёнки расширения (там же 5V/3V3/GND).
// Альтернатива для UART: GPIO17/GPIO18 или пады U0RXD=44 / U0TXD=43,
// но UART0 занят логами (см. DEBUG_MODE в logger.h).
#define BTN_PIN 0
#define VESC_RX_PIN 44  // VESC RX (пад GPIO3)
#define VESC_TX_PIN 43  // VESC TX (пад GPIO2)
#define BAUD 115200
HardwareSerial VSerial(1);  // создаём второй UART
VescUart UART;

// Одометр: Total — копится между запусками в NVS, Trip — от текущей загрузки
static const char *PREFS_NAMESPACE = "settings";
static const char *PREFS_KEY_TACHO_OFFSET = "tacho_off";
static const char *PREFS_KEY_COST_OFFSET = "cost_off";
// Замок и режим диагностики обязаны переживать выключение питания
static const char *PREFS_KEY_LOCK = "lock";
static const char *PREFS_KEY_DEBUG = "dbg";
static const unsigned long PERSIST_SAVE_INTERVAL_MS = 30000;  // сохраняем в NVS не чаще раза в 30с
static float tacho_offset_persisted = 0.0f;  // загруженный из NVS оффсет (raw VESC counts)
static float boot_tacho = 0.0f;              // первое VESC-показание после загрузки
static bool boot_tacho_set = false;
static float last_saved_total = 0.0f;
static unsigned long last_persist_save_ms = 0;

// Стоимость электроэнергии: считаем по VESC-телеметрии (wattHours - wattHoursCharged)
// Батарея: 21 А·ч × 48 В ≈ 1.008 кВт·ч на полный заряд (для справки, в расчёте не нужна).
static const float ELECTRICITY_RATE_ILS_PER_KWH = 0.68f;
static float cost_offset_persisted = 0.0f;   // загруженная из NVS накопленная стоимость, ILS
static float boot_net_wh = 0.0f;             // (wattHours - wattHoursCharged) на момент первого показания
static bool boot_net_wh_set = false;
static float last_saved_cost = 0.0f;

void setup() {
  LOG_BEGIN(BAUD);
  prefs.begin(PREFS_NAMESPACE, false);
  // Persistent total odometer (хранится как сумма raw VESC tachometerAbs counts)
  tacho_offset_persisted = prefs.getFloat(PREFS_KEY_TACHO_OFFSET, 0.0f);
  last_saved_total = tacho_offset_persisted;
  // Persistent накопленная стоимость электроэнергии (в шекелях)
  cost_offset_persisted = prefs.getFloat(PREFS_KEY_COST_OFFSET, 0.0f);
  last_saved_cost = cost_offset_persisted;
  // prefs остаётся открытым на всё время работы — сохраняемся периодически в loop()
  // Режим диагностики читаем ДО сборки UI: набор вкладок фиксируется в ui_build()
  ui_set_debug_enabled(prefs.getBool(PREFS_KEY_DEBUG, false));
  debug_saved_state = ui_get_debug_enabled();
  pinMode(BTN_PIN, INPUT_PULLUP);
  delay(200);
  LCD_Init();
  Lvgl_Init();
  ui_build();
  // Замок восстанавливаем после сборки — ui_set_lock() трогает виджеты.
  // Кламп в VESC при этом ещё не отправлен: это делает первая итерация loop().
  ui_set_lock(prefs.getBool(PREFS_KEY_LOCK, false));
  lock_saved_state = ui_get_lock();
  // UI уезжает в свою задачу FreeRTOS: свайпы и анимации больше не зависят
  // от того, сколько loop() ждёт ответа VESC
  Lvgl_Start_Task();

  VSerial.begin(BAUD, SERIAL_8N1, VESC_RX_PIN, VESC_TX_PIN);
  UART.setSerialPort(&VSerial);

  delay(200);
}

void loop() {
  // LVGL крутится в своей задаче (Lvgl_Start_Task), Timer_Loop() здесь не нужен.
  const unsigned long now_ms = millis();
  if (now_ms - lastFetch < VESC_POLL_INTERVAL_MS) {
    vTaskDelay(pdMS_TO_TICKS(10));  // отдаём процессор задаче UI
    return;
  }
  lastFetch = now_ms;

  const bool lock_on = ui_get_lock();

  int state = digitalRead(BTN_PIN);  // читаем состояние

  // Пока замок активен, ручные команды тяги не шлём — они дрались бы с тормозом
  if (state == LOW && !lock_on) {
    LOG_PRINTLN("Button held: test duty");
    UART.setDuty(0.03f);
  }

  // Лимит скорости и замок правят один и тот же пакет mcconf (он перезаписывает все
  // поля разом), поэтому собираем их в одну структуру. Шлём при любом изменении, а
  // пока что-то активно — повторяем раз в LIMIT_REFRESH_MS (store=0, значения не
  // переживают ребут контроллера). До getVescValues(), чтобы не мешать чтению ответа.
  const bool limit_on = ui_get_limit25();
  const bool guard_on = limit_on || lock_on;
  // На старте ничего не шлём, если замок снят и лимит выключен: сохранённые состояния
  // совпадают с *_sent_state, и конфиг VESC остаётся тем, что выставлен в VESC Tool.
  // А вот замок, поднятый из NVS, уезжает в контроллер на первой же итерации.
  if (limit_on != limit_sent_state || lock_on != lock_sent_state ||
      (guard_on && now_ms - last_limit_send_ms >= LIMIT_REFRESH_MS)) {
    VescLimits lim;
    lim.max_erpm = limit_on ? ui_kmh_to_erpm(TARGET_KMH) : NO_LIMIT_ERPM;
    lim.min_erpm = -lim.max_erpm;
    lim.current_min_scale = 1.0f;             // тормозную сторону не режем: ей держим замок
    lim.current_max_scale = lock_on ? 0.0f : 1.0f;  // 0 = газ не даёт тока
    vesc_send_limits(VSerial, lim);
    limit_sent_state = limit_on;
    lock_sent_state = lock_on;
    last_limit_send_ms = now_ms;
  }

  // Состояние замка и режима диагностики держим в NVS: события редкие, пишем сразу,
  // без 30-секундного троттлинга одометра.
  if (lock_on != lock_saved_state) {
    prefs.putBool(PREFS_KEY_LOCK, lock_on);
    lock_saved_state = lock_on;
    LOG_PRINTF("lock %s (saved)\n", lock_on ? "ON" : "OFF");
  }
  const bool debug_on = ui_get_debug_enabled();
  if (debug_on != debug_saved_state) {
    prefs.putBool(PREFS_KEY_DEBUG, debug_on);
    debug_saved_state = debug_on;
  }

  // Удерживающий тормоз замка — до чтения телеметрии и независимо от того, ответил
  // ли VESC: замок должен держать даже при сбоях связи.
  if (lock_on) UART.setBrakeCurrent(LOCK_BRAKE_A);

  if (UART.getVescValues()) {
    float voltage = UART.data.inpVoltage;
    float temp = UART.data.tempMosfet;
    float rpm = UART.data.rpm;
    float tachometerAbs = UART.data.tachometerAbs;
    float motorCurrent = UART.data.avgMotorCurrent;  // фазный ток мотора, А
    float inputCurrent = UART.data.avgInputCurrent;  // ток из батареи, А
    float wattHours = UART.data.wattHours;
    float wattHoursCharged = UART.data.wattHoursCharged;
    float net_wh = wattHours - wattHoursCharged;  // чистый расход батареи в Вт·ч

    // Привязываем baseline к первому валидному показанию VESC
    if (!boot_tacho_set) {
      boot_tacho = tachometerAbs;
      boot_tacho_set = true;
    }
    if (!boot_net_wh_set) {
      boot_net_wh = net_wh;
      boot_net_wh_set = true;
    }

    float trip_tacho = tachometerAbs - boot_tacho;
    float total_tacho = tacho_offset_persisted + trip_tacho;

    float trip_kwh = (net_wh - boot_net_wh) / 1000.0f;
    float total_cost = cost_offset_persisted + trip_kwh * ELECTRICITY_RATE_ILS_PER_KWH;

    // LVGL не потокобезопасен: рисуем только под мьютексом задачи UI
    if (Lvgl_Lock(100)) {
      ui_set_tachometerAbs(total_tacho);
      ui_set_tachometer(trip_tacho);
      ui_set_cost(total_cost);
      ui_set_currents(motorCurrent, inputCurrent);
      ui_set_power(voltage * inputCurrent);  // мощность на колесо, Вт
      ui_set_battery(voltage);
      ui_set_temp(temp);
      ui_set_speed(rpm);
      ui_set_diag((int)UART.data.error, UART.data.dutyCycleNow, UART.data.tempMotor, true);
      Lvgl_Unlock();
    } else {
      LOG_PRINTLN("LVGL lock timeout, skip UI update");
    }

    // Ток с вкладки 0 под замком не шлём — он тянул бы вперёд против тормоза
    const float set_current = ui_get_current_applied();
    if (!lock_on && set_current > 0.01f) {
      UART.setCurrent(set_current);  // повторяем каждый цикл: таймаут VESC ~1с
      LOG_PRINTF("setCurrent %.1f A\n", set_current);
    }

    unsigned long now = millis();
    if (now - last_persist_save_ms >= PERSIST_SAVE_INTERVAL_MS) {
      if (total_tacho != last_saved_total) {
        prefs.putFloat(PREFS_KEY_TACHO_OFFSET, total_tacho);
        last_saved_total = total_tacho;
      }
      if (total_cost != last_saved_cost) {
        prefs.putFloat(PREFS_KEY_COST_OFFSET, total_cost);
        last_saved_cost = total_cost;
      }
      last_persist_save_ms = now;
    }
  } else {
    // Ответа нет — на вкладке диагностики это видно как «VESC LINK: NO DATA»
    if (Lvgl_Lock(100)) {
      ui_set_diag(0, 0.0f, 0.0f, false);
      Lvgl_Unlock();
    }
  }

  // Без delay(200): темп задаёт VESC_POLL_INTERVAL_MS, а UI живёт в своей задаче
  vTaskDelay(pdMS_TO_TICKS(10));
}
