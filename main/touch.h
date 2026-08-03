#pragma once
#include <Arduino.h>

// CST816T capacitive touch on the Waveshare ESP32-S3-Touch-LCD-1.69.
// I2C shared with the IMU and RTC: SCL=GPIO10, SDA=GPIO11; address 0x15.
#define TP_I2C_SDA   11
#define TP_I2C_SCL   10
#define TP_PIN_RST   13
#define TP_PIN_INT   14
#define TP_I2C_ADDR  0x15

// true if the chip answered over I2C
bool Touch_Init(void);

// Returns true if a finger is on the screen; coordinates are in the display's
// logical frame (portrait 240x280).
bool Touch_Read(uint16_t *x, uint16_t *y);
