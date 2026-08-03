#include <Wire.h>

#include "display.h"
#include "logger.h"
#include "touch.h"

// CST816T registers
#define TP_REG_GESTURE     0x01
#define TP_REG_FINGER_NUM  0x02  // followed by: XH, XL, YH, YL
#define TP_REG_CHIP_ID     0xA7
#define TP_REG_DIS_AUTO_SLEEP 0xFE  // 0x01 = do not go to sleep

static bool tp_ready = false;

static bool tp_read(uint8_t reg, uint8_t *buf, size_t len) {
  Wire.beginTransmission(TP_I2C_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(true) != 0) return false;
  if (Wire.requestFrom((uint8_t)TP_I2C_ADDR, (uint8_t)len) != len) return false;
  for (size_t i = 0; i < len; i++) buf[i] = Wire.read();
  return true;
}

static bool tp_write(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(TP_I2C_ADDR);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission(true) == 0;
}

bool Touch_Init(void) {
  // INT is read as a plain input: the chip is polled from LVGL's read_cb, so no
  // interrupt is needed (an I2C read wakes the CST816T on its own).
  pinMode(TP_PIN_INT, INPUT_PULLUP);

  pinMode(TP_PIN_RST, OUTPUT);
  digitalWrite(TP_PIN_RST, LOW);
  delay(10);
  digitalWrite(TP_PIN_RST, HIGH);
  delay(60);  // the chip needs ~50 ms to initialise after reset

  Wire.begin(TP_I2C_SDA, TP_I2C_SCL, 400000);

  uint8_t chip_id = 0;
  tp_ready = tp_read(TP_REG_CHIP_ID, &chip_id, 1);
  if (!tp_ready) {
    LOG_PRINTLN("CST816T: no I2C answer");
    return false;
  }
  LOG_PRINTF("CST816T: chip id 0x%02X\n", chip_id);

  // Without this the chip falls asleep and the first touches are lost
  tp_write(TP_REG_DIS_AUTO_SLEEP, 0x01);
  return true;
}

bool Touch_Read(uint16_t *x, uint16_t *y) {
  if (!tp_ready) return false;

  uint8_t d[5];  // [0]=touch count, [1..2]=X, [3..4]=Y
  if (!tp_read(TP_REG_FINGER_NUM, d, sizeof(d))) return false;
  if ((d[0] & 0x0F) == 0) return false;

  uint16_t rx = (uint16_t)((d[1] & 0x0F) << 8) | d[2];
  uint16_t ry = (uint16_t)((d[3] & 0x0F) << 8) | d[4];

  // The touch panel reports coordinates in the native portrait frame (240x280),
  // which is also the logical one when LCD_PORTRAIT=1. Landscape needs a swap/mirror.
#if LCD_PORTRAIT
  *x = rx;
  *y = ry;
#else
  *x = ry;
  *y = (uint16_t)(LCD_PANEL_WIDTH - 1 - rx);
#endif

  if (*x >= LCD_WIDTH) *x = LCD_WIDTH - 1;
  if (*y >= LCD_HEIGHT) *y = LCD_HEIGHT - 1;
  return true;
}
