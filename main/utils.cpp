#include <Arduino.h>
#include "utils.h"

String formatK(uint64_t n) {
  // компактный формат (12.3K / 4.5M), чтобы влезало на экран
  if (n >= 1000000ULL) {
    double m = n / 1000000.0;
    char buf[16];
    snprintf(buf, sizeof(buf), "%.1fM", m);
    return String(buf);
  } else if (n >= 1000ULL) {
    double k = n / 1000.0;
    char buf[16];
    snprintf(buf, sizeof(buf), "%.1fK", k);
    return String(buf);
  }
  return String((unsigned long)n);
}