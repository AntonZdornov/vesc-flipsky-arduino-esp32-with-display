#pragma once
#include <Arduino.h>
#include <SPI.h>

// Board: Waveshare ESP32-S3-Touch-LCD-1.69 (ESP32-S3R8, ST7789V2 240x280).
// The panel is natively portrait, 240(W) x 280(H).
#define LCD_PANEL_WIDTH   240  // native panel width (columns)
#define LCD_PANEL_HEIGHT  280  // native panel height (rows)

// 1 = vertical (portrait, 240x280) - the current mode, the layout in ui.cpp is built
//     for it; 0 = landscape 280x240 (the layout is NOT designed for it).
#define LCD_PORTRAIT 1

#if LCD_PORTRAIT
  #define LCD_WIDTH   240 //LCD width  (logical)
  #define LCD_HEIGHT  280 //LCD height (logical)
#else
  #define LCD_WIDTH   280
  #define LCD_HEIGHT  240
#endif

// GPIO6/GPIO7 are not FSPI IOMUX pins on the ESP32-S3, the signal goes through the
// GPIO matrix - above ~40 MHz it is unstable.
#define SPIFreq                        40000000
#define EXAMPLE_PIN_NUM_MISO           -1   // MISO is not routed to the ST7789V2 on this board
#define EXAMPLE_PIN_NUM_MOSI           7    // LCD_MOSI / LCD_SDA
#define EXAMPLE_PIN_NUM_SCLK           6    // LCD_SCL
#define EXAMPLE_PIN_NUM_LCD_CS         5
#define EXAMPLE_PIN_NUM_LCD_DC         4
#define EXAMPLE_PIN_NUM_LCD_RST        8
#define EXAMPLE_PIN_NUM_BK_LIGHT       15   // LCD_BL
#define Frequency       1000
#define Resolution      10

// Address-window offsets inside the controller memory (240x320 on the ST7789).
// For a 240x280 panel the visible area starts at row 20, columns are not shifted.
// The offsets are already expressed in LOGICAL coordinates: in portrait the row shift
// falls on Y (RASET), in landscape (MADCTL with MV) on X (CASET).
// MADCTL: 0x00 = portrait; 0x60 (MX|MV) = landscape.
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
