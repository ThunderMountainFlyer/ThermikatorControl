#pragma once
#include <DallasTemperature.h>
#include <math.h>

// Non-blocking conversion for the existing first OneWire motor sensor.
// Only discovery and scratchpad reads take bus time; no conversion wait.
template <class TemperatureBus, unsigned long (*Clock)() = millis>
class AsyncMotorTemperature {
public:
  explicit AsyncMotorTemperature(TemperatureBus& bus) : bus(bus) {}

  void begin() {
    valid = false;
    pending = false;
    error = false;
    bus.begin();
    bus.setWaitForConversion(false);
    found = bus.getAddress(address, 0);
    if (!found) { error = true; return; }
    parasite = bus.isParasitePowerMode();
    maxConversionMs = bus.millisToWaitForConversion(bus.getResolution(address));
    start(); // first conversion immediately; following requests every ~5 s
  }

  bool poll() {
    if (!found) return false;
    bool updated = false;
    if (pending) {
      const uint32_t elapsed = Clock() - requestedAt;
      // Do not issue read slots while a parasite-powered device needs power.
      const bool ready = parasite ? elapsed >= maxConversionMs : bus.isConversionComplete();
      if (ready) {
        const float reading = bus.getTempC(address); // cached ROM, CRC checked by library
        pending = false;
        if (isfinite(reading) && reading != DEVICE_DISCONNECTED_C &&
            reading >= -55 && reading <= 125) {
          temperature = reading;
          valid = true;
          error = false;
          updated = true;
        } else error = true; // never replace a valid value with -127 C
      } else if (elapsed >= uint32_t(maxConversionMs) + 250UL) {
        pending = false;
        error = true; // conversion timeout; retry on the next regular request
      }
    }
    if (!pending && Clock() - requestedAt >= RequestPeriodMs) start();
    return updated;
  }

  bool hasValue() const { return valid; }
  bool faulted() const { return error; }
  float value() const { return temperature; }

private:
  static const uint32_t RequestPeriodMs = 5000;
  TemperatureBus& bus;
  DeviceAddress address = {};
  bool found = false, parasite = false, pending = false, valid = false, error = false;
  uint16_t maxConversionMs = 750;
  uint32_t requestedAt = 0;
  float temperature = 0;

  void start() {
    bus.requestTemperatures(); // returns after sending the command, not conversion
    requestedAt = Clock();
    pending = true;
  }
};

using MotorTemperature = AsyncMotorTemperature<DallasTemperature>;
