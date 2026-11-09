#pragma once
#include <stdint.h>
#include "Adafruit_GFX.h"
#include "SPI.h"

// Adafruit ST7789 1.11.0 API subset used by display.cpp; records into board().lcd.
// Colors: Adafruit_ST77xx.h (RGB565).
#define ST77XX_BLACK 0x0000
#define ST77XX_WHITE 0xFFFF
#define ST77XX_RED 0xF800
#define ST77XX_CYAN 0x07FF
#define ST77XX_YELLOW 0xFFE0

class Adafruit_ST7789 {
 public:
  Adafruit_ST7789(SPIClass *spi, int8_t cs, int8_t dc, int8_t rst);
  void init(uint16_t width, uint16_t height, uint8_t spiMode = 0);
  void setSPISpeed(uint32_t hz);
  void setRotation(uint8_t r);
  void setTextWrap(bool w);
  void enableSleep(bool on);
  void fillScreen(uint16_t c);
  void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c);
  void drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c);
  void fillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t c);
  void fillTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t c);
  void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t c);
  void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t c);
  void drawCircleHelper(int16_t x, int16_t y, int16_t r, uint8_t corner, uint16_t c);
  void setTextSize(uint8_t s) { textSize_ = s; }
  void setTextColor(uint16_t fg, uint16_t bg) { fg_ = fg; bg_ = bg; }
  void setCursor(int16_t x, int16_t y) { cx_ = x; cy_ = y; }
  size_t print(const char *s);
  void fillCircle(int16_t x, int16_t y, int16_t r, uint16_t c);
  int16_t getCursorX() const { return cx_; }
  int16_t getCursorY() const { return cy_; }
  void setFont(const GFXfont *f = nullptr) { font_ = f; }

 private:
  uint8_t textSize_ = 1;
  uint16_t fg_ = 0xFFFF, bg_ = 0;
  int16_t cx_ = 0, cy_ = 0;
  const GFXfont *font_ = nullptr;
};
