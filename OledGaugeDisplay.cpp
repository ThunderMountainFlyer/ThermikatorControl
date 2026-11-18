#include "OledGaugeDisplay.h"
#include "OledGaugeIcons.h"
#include "FirmwareVersion.h"
#include <math.h>
#include <string.h>

void OledGaugeDisplay::begin(uint8_t address) {
  oled.begin(&Adafruit128x64, address);
  first = true;
  sending = false;
  blinkingActive = false;
  blinkVisible = false;
  startup = Startup::Drawing;
  // Draw the version first, then hold it while USB detection runs.
  update(0, 20, 0, 100, false);
}

void OledGaugeDisplay::update(float vario, int temperature, int power, int battery,
                              bool autothrust) {
  // Non-blocking blink, serviced at the existing control-loop cadence.
  const unsigned long now = millis();
  // Warning thresholds use raw values, not rounded gauge segments.
  const bool hot = temperature > 81;
  const bool lowBattery = battery < 25;
  const bool blinkNeeded = autothrust || hot || lowBattery;
  if (!blinkNeeded) {
    blinkVisible = true;
  } else if (!blinkingActive) {
    blinkVisible = true; // start with a visible phase
    lastBlink = now;
  } else if (now - lastBlink >= 500) {
    blinkVisible = !blinkVisible;
    lastBlink = now;
  }
  blinkingActive = blinkNeeded;

  if (!isfinite(vario)) vario = 0;
  vario = constrain(vario, -2.0f, 2.0f);
  const float steps = vario * 2.5f;
  const uint8_t next[7] = {
    uint8_t(5 + int(steps + (steps >= 0 ? 0.5f : -0.5f))),
    uint8_t((constrain(power, 0, 100) + 5) / 10),
    uint8_t((constrain(temperature, 20, 90) - 20 + 3) / 7),
    uint8_t((constrain(battery, 0, 100) + 5) / 10),
    uint8_t(autothrust && blinkVisible ? 1 : 0),
    uint8_t(!hot || blinkVisible ? 1 : 0),
    uint8_t(!lowBattery || blinkVisible ? 1 : 0)
  };
  memcpy(wanted, next, sizeof wanted);
}

void OledGaugeDisplay::service() {
  if (startup == Startup::Holding) {
    if (startupElapsedMs() < FirmwareStartupWindowMs) return;
    startup = Startup::Complete;
  }
  if (!sending) {
    const bool gaugesChanged = first || memcmp(shown, wanted, 4) != 0 ||
      shown[5] != wanted[5] || shown[6] != wanted[6];
    if (startup == Startup::Complete && !gaugesChanged && shown[4] == wanted[4]) return;
    memcpy(frame, wanted, sizeof frame); // stable snapshot until fully transmitted
    pageCount = startup == Startup::Drawing || gaugesChanged ? 8 : 1;
    page = column = 0;
    sending = true;
  }
  if (column == 0) renderPage();
  oled.setCursor(column, page);
  for (uint8_t x = 0; x < 15; ++x) oled.ssd1306WriteRamBuf(pixels[column + x]);
  // Always close the Wire transaction before returning to the shared sensor bus.
  oled.ssd1306WriteRam(pixels[column + 15]);
  column += 16;
  if (column == 128) {
    column = 0;
    if (++page == pageCount) {
      if (startup == Startup::Drawing) {
        startup = Startup::Holding;
        startupStarted = millis(); // full version image is now on the OLED
      } else {
        memcpy(shown, frame, sizeof shown);
        first = false;
      }
      sending = false;
    }
  }
}

void OledGaugeDisplay::pixel(int16_t x, int16_t y) {
  if (x < 0 || x >= 128 || y < page * 8 || y >= (page + 1) * 8) return;
  pixels[x] |= uint8_t(1 << (y & 7));
}

void OledGaugeDisplay::rect(uint8_t x, uint8_t y, uint8_t width,
                            uint8_t height, bool filled) {
  const uint8_t top = max(y, uint8_t(page * 8));
  const uint8_t bottom = min(uint8_t(y + height), uint8_t((page + 1) * 8));
  for (uint8_t row = top; row < bottom; ++row)
    for (uint8_t col = 0; col < width; ++col)
      if (filled || row == y || row == y + height - 1 || col == 0 || col == width - 1)
        pixel(x + col, row);
}

void OledGaugeDisplay::text(const char* label, uint8_t x, uint8_t y, uint8_t scale) {
  if (y >= (page + 1) * 8 || y + 7 * scale <= page * 8) return;
  while (x < 128) {
    const uint8_t c = pgm_read_byte(label++);
    if (!c) break;
    if (c < 32 || c > 126) continue;
    for (uint8_t col = 0; col < 5; ++col) {
      const uint8_t bits = pgm_read_byte(System5x7 + 6 + (c - 32) * 5 + col);
      for (uint8_t row = 0; row < 7; ++row)
        if (bits & (1 << row)) {
          if (scale == 1) pixel(x + col, y + row);
          else rect(x + col * scale, y + row * scale, scale, scale, true);
        }
    }
    x += 6 * scale;
  }
}

void OledGaugeDisplay::icon(uint8_t index, uint8_t y) {
  for (uint8_t row = 0; row < 12; ++row) {
    if (y + row < page * 8 || y + row >= (page + 1) * 8) continue;
    for (uint8_t col = 0; col < 13; ++col)
      if (pgm_read_byte(&GaugeIcons[index][row * 2 + col / 8]) & (0x80 >> (col & 7)))
        pixel(1 + col, y + row);
  }
}

void OledGaugeDisplay::bar(uint8_t y, uint8_t value, bool bipolar) {
  for (uint8_t segment = 0; segment < 10; ++segment) {
    const bool filled = bipolar
      ? (value >= 5 ? segment >= 5 && segment < value : segment >= value && segment < 5)
      : segment < value;
    rect(18 + segment * 11, y, 8, 10, filled);
  }
  if (bipolar) rect(73, y - 1, 1, 12, true);
}

void OledGaugeDisplay::renderPage() {
  memset(pixels, 0, sizeof pixels);
  if (startup == Startup::Drawing) {
    constexpr uint8_t width = (sizeof(FIRMWARE_VERSION) - 1) * 6 - 1;
    static_assert(width <= 128, "Firmware version must fit the OLED");
    constexpr uint8_t scale = min(4, 128 / width);
    text(PSTR(FIRMWARE_VERSION), (128 - width * scale) / 2, (64 - 7 * scale) / 2, scale);
    return;
  }
  if (frame[4]) text(PSTR("Autothrust"), 34, 0);
  for (uint8_t row = 0; row < 4; ++row) {
    icon(row, 9 + row * 14);
    // Keep symbols visible while the entire warning bar flashes on/off.
    if ((row != 2 || frame[5]) && (row != 3 || frame[6]))
      bar(10 + row * 14, frame[row], row == 0);
  }
  if (frame[5]) rect(112, 37, 1, 12, true); // 80 C on the 20..90 C scale
}
