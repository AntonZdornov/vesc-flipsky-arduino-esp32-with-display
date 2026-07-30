#pragma once
#include <Arduino.h>
#include <SPI.h>

// Плата: Waveshare ESP32-S3-Touch-LCD-1.69 (ESP32-S3R8, ST7789V2 240x280).
// Панель нативно портретная 240(H) x 280(V).
#define LCD_PANEL_WIDTH   240  // нативная ширина панели (столбцы)
#define LCD_PANEL_HEIGHT  280  // нативная высота панели (строки)

// 1 = вертикально (портрет, 240x280) — текущий режим, под него сделана вёрстка
//     в ui.cpp; 0 = ландшафт 280x240 (вёрстка под него НЕ рассчитана).
#define LCD_PORTRAIT 1

#if LCD_PORTRAIT
  #define LCD_WIDTH   240 //LCD width  (логическая)
  #define LCD_HEIGHT  280 //LCD height (логическая)
#else
  #define LCD_WIDTH   280
  #define LCD_HEIGHT  240
#endif

// GPIO6/GPIO7 не являются IOMUX-пинами FSPI на ESP32-S3, сигнал идёт через
// GPIO-матрицу — выше ~40 МГц работает нестабильно.
#define SPIFreq                        40000000
#define EXAMPLE_PIN_NUM_MISO           -1   // у ST7789V2 на этой плате MISO не разведён
#define EXAMPLE_PIN_NUM_MOSI           7    // LCD_MOSI / LCD_SDA
#define EXAMPLE_PIN_NUM_SCLK           6    // LCD_SCL
#define EXAMPLE_PIN_NUM_LCD_CS         5
#define EXAMPLE_PIN_NUM_LCD_DC         4
#define EXAMPLE_PIN_NUM_LCD_RST        8
#define EXAMPLE_PIN_NUM_BK_LIGHT       15   // LCD_BL
#define Frequency       1000
#define Resolution      10

// Смещения окна адресации внутри памяти контроллера (у ST7789 она 240x320).
// Для панели 240x280 видимая область начинается со строки 20, столбцы без сдвига.
// Смещения задаются уже в ЛОГИЧЕСКИХ координатах: в портрете строчный сдвиг
// приходится на Y (RASET), в ландшафте (MADCTL с MV) — на X (CASET).
// MADCTL: 0x00 — портрет; 0x60 (MX|MV) — ландшафт.
#if LCD_PORTRAIT
  #define Offset_X 0
  #define Offset_Y 20
  #define LCD_MADCTL 0x00
#else
  #define Offset_X 20
  #define Offset_Y 0
  #define LCD_MADCTL 0x60
#endif


void LCD_SetCursor(uint16_t x1, uint16_t y1, uint16_t x2,uint16_t y2);

void LCD_Init(void);
void LCD_FillScreen(uint16_t color);
void LCD_SetCursor(uint16_t Xstart, uint16_t Ystart, uint16_t Xend, uint16_t  Yend);
void LCD_addWindow(uint16_t Xstart, uint16_t Ystart, uint16_t Xend, uint16_t Yend,uint16_t* color);

void Backlight_Init(void);
void Set_Backlight(uint8_t Light);
