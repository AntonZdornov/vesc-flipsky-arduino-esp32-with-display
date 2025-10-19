#ifndef LOGGER_H
#define LOGGER_H

#include <Arduino.h>

// Set 0 for disable logs
#define DEBUG_MODE 1

#if DEBUG_MODE
  #define LOG_BEGIN(baud) Serial.begin(baud)
  #define LOG_PRINT(x) Serial.print(x)
  #define LOG_PRINTLN(x) Serial.println(x)
  #define LOG_PRINTF(...) Serial.printf(__VA_ARGS__)
#else
  #define LOG_BEGIN(baud)
  #define LOG_PRINT(x)
  #define LOG_PRINTLN(x)
  #define LOG_PRINTF(...)
#endif

#endif // LOGGER_H