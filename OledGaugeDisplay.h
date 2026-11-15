#pragma once

#include <SSD1306AsciiWire.h>

// Existing 128 x 64 SSD1306 OLED on I2C. No full-screen framebuffer.
class OledGaugeDisplay {
public:
  void begin(uint8_t address);
  void update(float vario, int temperature, int power, int battery, bool autothrust);

private:
  SSD1306AsciiWire oled;
  uint8_t page = 0;
  uint8_t pixels[128] = {};
  uint8_t shown[5] = {};
  bool first = true;
  bool autothrustActive = false;
  bool blinkVisible = false;
  unsigned long lastBlink = 0;

  void pixel(int16_t x, int16_t y);
  void rect(uint8_t x, uint8_t y, uint8_t width, uint8_t height, bool filled);
  void text(const char* label, uint8_t x, uint8_t y);
  void icon(uint8_t index, uint8_t y);
  void bar(uint8_t y, uint8_t value, bool bipolar);
  void renderPage();
};
