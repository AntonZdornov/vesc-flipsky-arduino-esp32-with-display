#include "vesc_limit.h"
#include "logger.h"

// Из библиотеки VescUart (её src/ лежит на include path)
#include <datatypes.h>
#include <buffer.h>
#include <crc.h>

// Поля mcconf, которые COMM_SET_MCCONF_TEMP перезаписывает вместе с нашими.
// Прошивка берёт текущий конфиг и подменяет только эти поля, поэтому здесь стоят
// «широкие» дефолты VESC — они ничего не режут.
static const float DUTY_MIN = 0.005f;         // l_min_duty (дефолт VESC)
static const float DUTY_MAX = 0.95f;          // l_max_duty (дефолт VESC)
static const float WATT_LIMIT = 1500000.0f;   // l_watt_min/max (дефолт = «без лимита»)

void vesc_send_limits(Stream &port, const VescLimits &lim) {
  uint8_t payload[64];
  int32_t ind = 0;

  payload[ind++] = COMM_SET_MCCONF_TEMP;
  payload[ind++] = 0;  // store: 0 — не писать в флеш (иначе убьём ресурс флеша)
  payload[ind++] = 1;  // forward_can: продублировать на слейвы по CAN
  payload[ind++] = 0;  // ack: ответ не нужен, иначе он влезет в чтение телеметрии
  payload[ind++] = 0;  // divide_by_controllers

  buffer_append_float32_auto(payload, lim.current_min_scale, &ind);  // l_current_min_scale
  buffer_append_float32_auto(payload, lim.current_max_scale, &ind);  // l_current_max_scale
  buffer_append_float32_auto(payload, lim.min_erpm, &ind);           // l_min_erpm
  buffer_append_float32_auto(payload, lim.max_erpm, &ind);           // l_max_erpm
  buffer_append_float32_auto(payload, DUTY_MIN, &ind);       // l_min_duty
  buffer_append_float32_auto(payload, DUTY_MAX, &ind);       // l_max_duty
  buffer_append_float32_auto(payload, -WATT_LIMIT, &ind);    // l_watt_min
  buffer_append_float32_auto(payload, WATT_LIMIT, &ind);     // l_watt_max

  // Кадр VESC: 0x02, длина, payload, CRC16 (big-endian), 0x03
  const uint16_t crc = crc16(payload, (unsigned int)ind);
  uint8_t frame[72];
  int n = 0;
  frame[n++] = 2;
  frame[n++] = (uint8_t)ind;
  memcpy(frame + n, payload, ind);
  n += ind;
  frame[n++] = (uint8_t)(crc >> 8);
  frame[n++] = (uint8_t)(crc & 0xFF);
  frame[n++] = 3;

  port.write(frame, n);
  LOG_PRINTF("VESC limits: erpm %.0f..%.0f, scale %.2f/%.2f\n", lim.min_erpm, lim.max_erpm,
             lim.current_min_scale, lim.current_max_scale);
}
