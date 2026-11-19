#pragma once
#include <Adafruit_BMP280.h>
#include <Wire.h>
#include <math.h>
#include "FrameTiming.h"

// Explicit forced conversions: every altitude belongs to a fresh, timed frame.
// Same x16/x16 oversampling as the previous Adafruit defaults; no extra IIR.
class VarioSampler {
public:
  explicit VarioSampler(Adafruit_BMP280& sensor) : sensor(sensor) {}
  bool begin() {
    available = sensor.begin(BMP280_ADDRESS_ALT);
    waiting = false;
    good = false;
    if (!available) return false;
    sensor.setSampling(Adafruit_BMP280::MODE_SLEEP,
      Adafruit_BMP280::SAMPLING_X16, Adafruit_BMP280::SAMPLING_X16,
      Adafruit_BMP280::FILTER_OFF, Adafruit_BMP280::STANDBY_MS_1);
    // Allow the previous normal conversion to finish without a blocking wait.
    nextStart = micros() + 100000UL;
    return true;
  }

  bool poll() {
    if (!available) return false;
    const uint32_t now = micros();
    if (waiting && deadlineReached(now, startedAt + VarioReadDelayUs)) {
      uint8_t status;
      if (!readRegister(0xF3, status)) { fail(); return false; }
      if (status & 0x08) {
        if (now - startedAt >= 110000UL) fail();
        return false;
      }
      Wire.clearWireTimeoutFlag();
      const float pressure = sensor.readPressure();
      waiting = false;
      if (Wire.getWireTimeoutFlag() || !isfinite(pressure) || pressure < 30000 || pressure > 110000) {
        fail(); return false;
      }
      height = 44330.0f * (1.0f - pow(pressure / 101325.0f, 0.1903f));
      good = isfinite(height);
      return good;
    }
    if (!waiting && deadlineReached(now, nextStart)) {
      const uint32_t lateness = now - nextStart;
      if (lateness >= VarioFrameUs) { ++missed; good = false; }
      nextStart = followingFrame(nextStart, now);
      Wire.beginTransmission(BMP280_ADDRESS_ALT);
      Wire.write(0xF4);
      Wire.write(0xB5); // osrs_t=x16, osrs_p=x16, forced mode
      if (Wire.endTransmission() != 0) { fail(); return false; }
      startedAt = micros();
      startLateness = lateness;
      waiting = true;
    }
    return false;
  }

  uint32_t spareUs() const {
    if (!available) return VarioFrameUs;
    return remainingTime(micros(), waiting ? startedAt + VarioReadDelayUs : nextStart);
  }
  float altitude() const { return height; }
  uint32_t sampleTimeUs() const { return startedAt; }
  uint32_t latenessUs() const { return startLateness; }
  bool valid() const { return good; }
  uint16_t missedFrames() const { return missed; }
  uint16_t errors() const { return errorCount; }

private:
  Adafruit_BMP280& sensor;
  bool available = false, waiting = false, good = false;
  uint32_t nextStart = 0, startedAt = 0, startLateness = 0;
  uint16_t missed = 0, errorCount = 0;
  float height = 0;
  void fail() { waiting = false; good = false; ++errorCount; }
  bool readRegister(uint8_t reg, uint8_t& value) {
    Wire.beginTransmission(BMP280_ADDRESS_ALT); Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom(uint8_t(BMP280_ADDRESS_ALT), uint8_t(1)) != 1) return false;
    value = Wire.read();
    return true;
  }
};
