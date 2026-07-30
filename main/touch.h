#pragma once
#include <Arduino.h>

// Ёмкостный тач CST816T на Waveshare ESP32-S3-Touch-LCD-1.69.
// I2C общий с IMU и RTC: SCL=GPIO10, SDA=GPIO11; адрес 0x15.
#define TP_I2C_SDA   11
#define TP_I2C_SCL   10
#define TP_PIN_RST   13
#define TP_PIN_INT   14
#define TP_I2C_ADDR  0x15

// true, если чип ответил по I2C
bool Touch_Init(void);

// Возвращает true, если палец на экране; координаты — в логических
// координатах дисплея (портрет 240x280).
bool Touch_Read(uint16_t *x, uint16_t *y);
