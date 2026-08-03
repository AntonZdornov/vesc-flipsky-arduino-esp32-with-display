#include "display.h"

#define SPI_WRITE(_dat) SPI.transfer(_dat)
#define SPI_WRITE_Word(_dat) SPI.transfer16(_dat)

// display Model: ST7789V2 (240x280), Waveshare ESP32-S3-Touch-LCD-1.69

void SPI_Init() {
  // MISO = -1: the line is not routed, SPI is write-only
  SPI.begin(EXAMPLE_PIN_NUM_SCLK, EXAMPLE_PIN_NUM_MISO, EXAMPLE_PIN_NUM_MOSI);
}

void LCD_WriteCommand(uint8_t Cmd) {
  SPI.beginTransaction(SPISettings(SPIFreq, MSBFIRST, SPI_MODE0));
  digitalWrite(EXAMPLE_PIN_NUM_LCD_CS, LOW);
  digitalWrite(EXAMPLE_PIN_NUM_LCD_DC, LOW);
  SPI_WRITE(Cmd);
  digitalWrite(EXAMPLE_PIN_NUM_LCD_CS, HIGH);
  SPI.endTransaction();
}
void LCD_WriteData(uint8_t Data) {
  SPI.beginTransaction(SPISettings(SPIFreq, MSBFIRST, SPI_MODE0));
  digitalWrite(EXAMPLE_PIN_NUM_LCD_CS, LOW);
  digitalWrite(EXAMPLE_PIN_NUM_LCD_DC, HIGH);
  SPI_WRITE(Data);
  digitalWrite(EXAMPLE_PIN_NUM_LCD_CS, HIGH);
  SPI.endTransaction();
}
void LCD_WriteData_Word(uint16_t Data) {
  SPI.beginTransaction(SPISettings(SPIFreq, MSBFIRST, SPI_MODE0));
  digitalWrite(EXAMPLE_PIN_NUM_LCD_CS, LOW);
  digitalWrite(EXAMPLE_PIN_NUM_LCD_DC, HIGH);
  SPI_WRITE_Word(Data);
  digitalWrite(EXAMPLE_PIN_NUM_LCD_CS, HIGH);
  SPI.endTransaction();
}
void LCD_WriteData_nbyte(uint8_t* SetData, uint8_t* ReadData, uint32_t Size) {
  SPI.beginTransaction(SPISettings(SPIFreq, MSBFIRST, SPI_MODE0));
  digitalWrite(EXAMPLE_PIN_NUM_LCD_CS, LOW);
  digitalWrite(EXAMPLE_PIN_NUM_LCD_DC, HIGH);
  // ReadData == NULL - write only, no receive buffer needed
  SPI.transferBytes(SetData, ReadData, Size);
  digitalWrite(EXAMPLE_PIN_NUM_LCD_CS, HIGH);
  SPI.endTransaction();
}

void LCD_Reset(void) {
  digitalWrite(EXAMPLE_PIN_NUM_LCD_CS, LOW);
  delay(50);
  digitalWrite(EXAMPLE_PIN_NUM_LCD_RST, LOW);
  delay(50);
  digitalWrite(EXAMPLE_PIN_NUM_LCD_RST, HIGH);
  delay(50);
}

static inline void LCD_WriteBytes(const uint8_t* data, uint32_t len) {
  SPI.beginTransaction(SPISettings(SPIFreq, MSBFIRST, SPI_MODE0));
  digitalWrite(EXAMPLE_PIN_NUM_LCD_CS, LOW);
  digitalWrite(EXAMPLE_PIN_NUM_LCD_DC, HIGH);

  // Write sequentially; maximally compatible
  while (len--) {
    SPI.transfer(*data++);
  }

  digitalWrite(EXAMPLE_PIN_NUM_LCD_CS, HIGH);
  SPI.endTransaction();
}

void LCD_FillScreen(uint16_t color) {  // no 'static' here so it matches the header
    const uint16_t W = LCD_WIDTH;
    const uint16_t H = LCD_HEIGHT;

    static uint16_t line[LCD_WIDTH];
    for (uint16_t i = 0; i < W; ++i) line[i] = color;

    for (uint16_t y = 0; y < H; ++y) {
        LCD_SetCursor(0, y, W - 1, y);
        LCD_WriteBytes((const uint8_t*)line, W * sizeof(uint16_t)); // the right function
    }
}

void LCD_Init(void) {
  pinMode(EXAMPLE_PIN_NUM_LCD_CS, OUTPUT);
  pinMode(EXAMPLE_PIN_NUM_LCD_DC, OUTPUT);
  pinMode(EXAMPLE_PIN_NUM_LCD_RST, OUTPUT);
  Backlight_Init();
  SPI_Init();

  LCD_Reset();
  //************* Start Initial Sequence **********//
  LCD_WriteCommand(0x11);
  delay(120);
  LCD_WriteCommand(0x36);
  // Orientation is set by LCD_PORTRAIT in display.h (currently portrait, MADCTL 0x00).
  // If the image comes out rotated by 180 degrees, try 0xC0 (MX|MY) in portrait
  // and 0xA0 (MY|MV) in landscape.
  LCD_WriteData(LCD_MADCTL);

  LCD_WriteCommand(0x3A);
  LCD_WriteData(0x05);

  LCD_WriteCommand(0xB0);
  LCD_WriteData(0x00);
  LCD_WriteData(0xE8);

  LCD_WriteCommand(0xB2);
  LCD_WriteData(0x0C);
  LCD_WriteData(0x0C);
  LCD_WriteData(0x00);
  LCD_WriteData(0x33);
  LCD_WriteData(0x33);

  LCD_WriteCommand(0xB7);
  LCD_WriteData(0x35);

  LCD_WriteCommand(0xBB);
  LCD_WriteData(0x35);

  LCD_WriteCommand(0xC0);
  LCD_WriteData(0x2C);

  LCD_WriteCommand(0xC2);
  LCD_WriteData(0x01);

  LCD_WriteCommand(0xC3);
  LCD_WriteData(0x13);

  LCD_WriteCommand(0xC4);
  LCD_WriteData(0x20);

  LCD_WriteCommand(0xC6);
  LCD_WriteData(0x0F);

  LCD_WriteCommand(0xD0);
  LCD_WriteData(0xA4);
  LCD_WriteData(0xA1);

  LCD_WriteCommand(0xD6);
  LCD_WriteData(0xA1);

  LCD_WriteCommand(0xE0);
  LCD_WriteData(0xF0);
  LCD_WriteData(0x00);
  LCD_WriteData(0x04);
  LCD_WriteData(0x04);
  LCD_WriteData(0x04);
  LCD_WriteData(0x05);
  LCD_WriteData(0x29);
  LCD_WriteData(0x33);
  LCD_WriteData(0x3E);
  LCD_WriteData(0x38);
  LCD_WriteData(0x12);
  LCD_WriteData(0x12);
  LCD_WriteData(0x28);
  LCD_WriteData(0x30);

  LCD_WriteCommand(0xE1);
  LCD_WriteData(0xF0);
  LCD_WriteData(0x07);
  LCD_WriteData(0x0A);
  LCD_WriteData(0x0D);
  LCD_WriteData(0x0B);
  LCD_WriteData(0x07);
  LCD_WriteData(0x28);
  LCD_WriteData(0x33);
  LCD_WriteData(0x3E);
  LCD_WriteData(0x36);
  LCD_WriteData(0x14);
  LCD_WriteData(0x14);
  LCD_WriteData(0x29);
  LCD_WriteData(0x32);

  LCD_WriteCommand(0x21); // swapped the inversion to get rid of it
  // LCD_WriteCommand(0x20); // inversion

  LCD_WriteCommand(0x11);
  delay(120);
  LCD_WriteCommand(0x29);

  
}
/******************************************************************************
function: Set the cursor position
parameter :
    Xstart:   Start uint16_t x coordinate
    Ystart:   Start uint16_t y coordinate
    Xend  :   End uint16_t coordinates
    Yend  :   End uint16_t coordinatesen
******************************************************************************/
static void LCD_WriteAddrRange(uint8_t cmd, uint16_t start, uint16_t end) {
  // The offset is added BEFORE splitting into bytes: the address reaches 279+20 = 299,
  // so the high byte is no longer zero.
  LCD_WriteCommand(cmd);
  LCD_WriteData(start >> 8);
  LCD_WriteData(start & 0xFF);
  LCD_WriteData(end >> 8);
  LCD_WriteData(end & 0xFF);
}

void LCD_SetCursor(uint16_t Xstart, uint16_t Ystart, uint16_t Xend, uint16_t Yend) {
  // Offset_X/Offset_Y are given in the logical coordinates of the current orientation,
  // so the mapping is direct: X -> CASET (0x2A), Y -> RASET (0x2B).
  LCD_WriteAddrRange(0x2A, Xstart + Offset_X, Xend + Offset_X);
  LCD_WriteAddrRange(0x2B, Ystart + Offset_Y, Yend + Offset_Y);
  LCD_WriteCommand(0x2C);
}
/******************************************************************************
function: Refresh the image in an area
parameter :
    Xstart:   Start uint16_t x coordinate
    Ystart:   Start uint16_t y coordinate
    Xend  :   End uint16_t coordinates
    Yend  :   End uint16_t coordinates
    color :   Set the color
******************************************************************************/
void LCD_addWindow(uint16_t Xstart, uint16_t Ystart, uint16_t Xend, uint16_t Yend, uint16_t* color) {
  // uint16_t i,j;
  // LCD_SetCursor(Xstart, Ystart, Xend,Yend);
  // uint16_t Show_Width = Xend - Xstart + 1;
  // uint16_t Show_Height = Yend - Ystart + 1;
  // for(i = 0; i < Show_Height; i++){
  //   for(j = 0; j < Show_Width; j++){
  //     LCD_WriteData_Word(color[(i*(Show_Width))+j]);
  //   }
  // }
  uint16_t Show_Width = Xend - Xstart + 1;
  uint16_t Show_Height = Yend - Ystart + 1;
  uint32_t numBytes = (uint32_t)Show_Width * Show_Height * sizeof(uint16_t);
  LCD_SetCursor(Xstart, Ystart, Xend, Yend);
  // No receive buffer is allocated (there used to be a VLA on the stack - with an
  // LVGL buffer of 280x240/20 that is ~6.7 KB and it blew the loopTask stack).
  LCD_WriteData_nbyte((uint8_t*)color, NULL, numBytes);
}
// backlight
void Backlight_Init(void) {
  ledcAttach(EXAMPLE_PIN_NUM_BK_LIGHT, Frequency, Resolution);
  ledcWrite(EXAMPLE_PIN_NUM_BK_LIGHT, 972);  // 95% * 1023 (10-bit) ≈ 972
}

void Set_Backlight(uint8_t Light) {
  if (Light > 100 || Light < 0)
    printf("Set Backlight parameters in the range of 0 to 100 \r\n");
  else {
    uint32_t Backlight = Light * 10;
    ledcWrite(EXAMPLE_PIN_NUM_BK_LIGHT, Backlight);
  }
}
