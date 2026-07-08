#include <Arduino.h>
#include <HardwareSerial.h>
#include "VescUart.h"
#include "display.h"
#include "lvgl_driver.h"
#include "logger.h"
#include "utils.h"
#include "ui.h"
#include "ui_globals.h"
#include <lvgl.h>
#include <Preferences.h>

Preferences prefs;

unsigned long lastFetch = 0;

// ВКЛ/ВЫКЛ режима лимита (можно переключать кнопкой)
static bool limit25_enabled = true;
static const float TARGET_KMH = 25.0f;  // лимит скорости

#define BTN_PIN 0
#define VESC_RX_PIN 3  // VESC RX
#define VESC_TX_PIN 2  // VESC TX
#define BAUD 115200
HardwareSerial VSerial(1);  // создаём второй UART
VescUart UART;

// Одометр: Total — копится между запусками в NVS, Trip — от текущей загрузки
static const char *PREFS_NAMESPACE = "settings";
static const char *PREFS_KEY_TACHO_OFFSET = "tacho_off";
static const char *PREFS_KEY_COST_OFFSET = "cost_off";
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
  pinMode(BTN_PIN, INPUT_PULLUP);
  delay(200);
  LCD_Init();
  Lvgl_Init();
  ui_build();

  VSerial.begin(BAUD, SERIAL_8N1, VESC_RX_PIN, VESC_TX_PIN);
  UART.setSerialPort(&VSerial);

  delay(200);
}

void loop() {
  Timer_Loop();
  int state = digitalRead(BTN_PIN);  // читаем состояние

  if (state == LOW) {
    LOG_PRINTLN("Set limit: 25km/h");
    const int erpm_limit = (int)kmh_to_erpm(TARGET_KMH);

    // // 2) Получаем желаемую «скорость» с твоего источника:
    // //    например, ручка газа → целевой eRPM (0..max_cmd)
    // //    ниже просто пример: плавно крутим от 0 до 9k eRPM
    // static int demo_cmd = 0;
    // demo_cmd += 50;
    // if (demo_cmd > 9000) demo_cmd = 0;
    // int target_erpm = demo_cmd;

    // // 3) Клемп по лимиту, если режим включён
    // if (limit25_enabled && target_erpm > erpm_limit) {
    //   target_erpm = erpm_limit;
    // }

    // 4) Отправляем в VESC
    // UART.setRPM(1000);
    UART.setDuty(0.03f);
  }

  if (UART.getVescValues()) {
    float voltage = UART.data.inpVoltage;
    float temp = UART.data.tempMosfet;
    float rpm = UART.data.rpm;
    float tachometerAbs = UART.data.tachometerAbs;
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

    ui_set_tachometerAbs(total_tacho);
    ui_set_tachometer(trip_tacho);
    ui_set_cost(total_cost);
    ui_set_battery(voltage);
    ui_set_temp(temp);
    ui_set_speed(rpm);

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
  }

  delay(200);
}
