// Standalone ATmega32U4 bench diagnostic. Never requests motor power.
// OLED sources in this folder are an unmodified snapshot of NewUIDesing
// at commit 3dee895. Keep them identical when comparing firmware revisions.
#include <Wire.h>
#include <Adafruit_BMP280.h>
#include <DallasTemperature.h>
#include <Servo.h>
#include "OledGaugeDisplay.h"
#include <math.h>
#include "MotorTemperature.h"

// The original full baseline is still available with this flag set to 1.
// The async edition omits only the old large-font text tests to fit the AVR.
#ifndef LEGACY_BASELINE
#define LEGACY_BASELINE 0
#endif

#if !defined(__AVR_ATmega32U4__)
#error Select the actual ATmega32U4 board (current hardware: Arduino Yun).
#endif

const uint8_t Samples = 8;
const uint8_t OledAddress = 0x3C;
const uint8_t TemperaturePin = 8, MotorPin = 9, AutothrustPin = 10, PotiPin = 18;
const uint8_t MinimumMotorCommand = 50;
const uint32_t I2cClock = 100000UL;
const uint16_t OriginalPauseMs = 638;
const float ReferencePressure = 1013.25f;

Adafruit_BMP280 bmp;
OneWire oneWire(TemperaturePin);
DallasTemperature sensors(&oneWire);
Servo motor;
OledGaugeDisplay display;
#if LEGACY_BASELINE
SSD1306AsciiWire legacy;
#endif
bool haveBmp = false, haveTemperature = false, haveOled = false;
bool reported = false;
bool switchRaw = false, switchStable = false, switchArmed = false;
unsigned long switchChangedAt = 0;
uint8_t forcedControl = 0, cycleMode = 0, cycleIndex = 0;
volatile float measuredFloat;
volatile int measuredInt;

struct Stats {
  uint32_t shortest = UINT32_MAX, longest = 0, sum = 0;
  uint16_t count = 0, errors = 0;
  void add(uint32_t us, bool valid) {
    if (us < shortest) shortest = us;
    if (us > longest) longest = us;
    sum += us;
    ++count;
    if (!valid) ++errors;
  }
};

void summary(const __FlashStringHelper* name, const Stats& s) {
  if (!s.count) return;
  Serial.print(F("summary;")); Serial.print(name); Serial.print(';');
  Serial.print(s.count); Serial.print(';'); Serial.print(s.shortest);
  Serial.print(';'); Serial.print(s.sum / s.count); Serial.print(';');
  Serial.print(s.longest); Serial.print(';'); Serial.println(s.errors);
}

// All preparation, waits between tests, USB formatting and serial transfers
// happen OUTSIDE the timed call. Interrupts remain enabled as in live firmware.
void bench(const __FlashStringHelper* name, void (*prepare)(), bool (*action)()) {
  Stats s;
  for (uint8_t sample = 0; sample < Samples; ++sample) {
    if (prepare) prepare();
    const uint32_t start = micros();
    const bool valid = action();
    const uint32_t elapsed = micros() - start;
    s.add(elapsed, valid);
    Serial.print(F("raw;")); Serial.print(name); Serial.print(';');
    Serial.print(sample + 1); Serial.print(';'); Serial.print(elapsed);
    Serial.print(';'); Serial.println(valid ? 1 : 0);
    Serial.flush();
  }
  summary(name, s);
}

void bench(const __FlashStringHelper* name, bool (*action)()) {
  bench(name, nullptr, action);
}

bool probe(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

bool readBmpRegister(uint8_t reg, uint8_t& value) {
  Wire.beginTransmission(BMP280_ADDRESS_ALT);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(uint8_t(BMP280_ADDRESS_ALT), uint8_t(1)) != 1) return false;
  value = Wire.read();
  return true;
}

bool writeBmpRegister(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(BMP280_ADDRESS_ALT);
  Wire.write(reg); Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool waitBmpIdle() {
  const uint32_t started = millis();
  while (millis() - started < 1000UL) {
    uint8_t status;
    if (!readBmpRegister(0xF3, status)) return false;
    if (!(status & 0x08)) return true;
    delay(1);
  }
  return false;
}

void forcedConversionBench() {
  uint8_t originalControl;
  if (!readBmpRegister(0xF4, originalControl)) {
    Serial.println(F("# ERROR: BMP280 ctrl read"));
    return;
  }
  Serial.print(F("# BMP280 ctrl_meas: 0x"));
  Serial.println(originalControl, HEX);
  // Keep exactly the oversampling/filter settings selected by bmp.begin().
  const uint8_t sleepingControl = originalControl & 0xFC;
  if (!writeBmpRegister(0xF4, sleepingControl) || !waitBmpIdle()) {
    writeBmpRegister(0xF4, originalControl);
    Serial.println(F("# ERROR: BMP280 idle"));
    return;
  }
  forcedControl = sleepingControl;
  bench(F("bmp_forced_conversion"), [](){
    const uint32_t started = micros();
    if (!writeBmpRegister(0xF4, forcedControl | 0x01)) return false;
    // Avoid mistaking a not-yet-asserted measuring bit for completion.
    delay(2);
    while (micros() - started < 1000000UL) {
      uint8_t status;
      if (!readBmpRegister(0xF3, status)) return false;
      if (!(status & 0x08)) return true;
      delay(1); // completion timing uncertainty approximately 1 ms + I2C poll
    }
    return false;
  });
  const bool restored = writeBmpRegister(0xF4, originalControl) && waitBmpIdle();
  Serial.println(restored ? F("# BMP280 mode restored") : F("# ERROR: BMP280 restore"));
  if (!restored) haveBmp = false;
}

void componentBenchmarks() {
  bench(F("timer_overhead"), [](){return true;});
  bench(F("potentiometer_read"), [](){measuredInt = analogRead(PotiPin); return true;});
  bench(F("autothrust_switch_read"), [](){measuredInt = digitalRead(AutothrustPin); return true;});
  bench(F("servo_write_minimum"), [](){motor.write(MinimumMotorCommand); return true;});

  if (haveTemperature) {
    bench(F("temperature_request_blocking"), [](){sensors.requestTemperatures(); return true;});
    bench(F("temperature_read_by_index"), [](){
      measuredFloat = sensors.getTempCByIndex(0);
      return measuredFloat != DEVICE_DISCONNECTED_C;
    });
    bench(F("temperature_request_plus_read"), [](){
      sensors.requestTemperatures();
      measuredFloat = sensors.getTempCByIndex(0);
      return measuredFloat != DEVICE_DISCONNECTED_C;
    });
  } else Serial.println(F("# SKIP temperature: no sensor at pin 8"));

  if (haveBmp) {
    bench(F("bmp_temperature_register_read"), [](){measuredFloat = bmp.readTemperature(); return isfinite(measuredFloat);});
    bench(F("bmp_pressure_register_read"), [](){measuredFloat = bmp.readPressure(); return isfinite(measuredFloat) && measuredFloat > 0;});
    bench(F("bmp_altitude_read_and_calculation"), [](){measuredFloat = bmp.readAltitude(ReferencePressure); return isfinite(measuredFloat);});
    forcedConversionBench();
  } else Serial.println(F("# SKIP BMP280: not found at 0x76"));
}

void displayBenchmarks() {
  if (!haveOled) { Serial.println(F("# SKIP OLED: no ACK")); return; }
#if LEGACY_BASELINE
  legacy.begin(&Adafruit128x64, OledAddress);
  legacy.setFont(fixed_bold10x15);
  bench(F("legacy_oled_clear"), [](){legacy.clear(); return true;});
  bench(F("legacy_oled_four_text_lines"), [](){
    legacy.setCursor(0, 0);
    legacy.println(F("Vario  0.0")); legacy.println(F("Temp   40"));
    legacy.println(F("Power  50")); legacy.println(F("Akku   100"));
    return true;
  });
#endif
  bench(F("gauge_display_begin"), [](){display.begin(OledAddress); return true;});
  bench(F("gauge_unchanged"), [](){display.update(0, 40, 50, 100, false);},
    [](){display.update(0, 40, 50, 100, false); return true;});
  bench(F("gauge_full_update"), [](){display.update(0, 40, 0, 100, false);},
    [](){display.update(0, 40, 100, 100, false); return true;});
  bench(F("gauge_header_blink"), [](){
    display.update(0, 40, 50, 100, false);
    display.update(0, 40, 50, 100, true);
    delay(510);
  }, [](){display.update(0, 40, 50, 100, true); return true;});
  bench(F("gauge_warning_blink"), [](){
    display.update(0, 40, 50, 100, false);
    display.update(0, 85, 50, 20, false);
    delay(510);
  }, [](){display.update(0, 85, 50, 20, false); return true;});
}

#if !LEGACY_BASELINE
// Instrument the actual bus calls while using the byte-identical production
// async reader. No serial output occurs within the acquisition phases.
class TimedTemperatureBus : public DallasTemperature {
public:
  explicit TimedTemperatureBus(OneWire* wire) : DallasTemperature(wire) {}
  Stats requestTime, probeTime, readTime, requestInterval;
  uint32_t lastRequest = 0;
  uint16_t requests = 0, reads = 0, probes = 0;
  void requestTemperatures() {
    const uint32_t start = micros();
    DallasTemperature::requestTemperatures();
    const uint32_t completed = micros();
    requestTime.add(completed - start, true);
    if (requests) requestInterval.add(completed - lastRequest, true);
    lastRequest = completed;
    ++requests;
  }
  bool isConversionComplete() {
    const uint32_t start = micros();
    const bool ready = DallasTemperature::isConversionComplete();
    probeTime.add(micros() - start, true); // busy is not an error
    ++probes;
    return ready;
  }
  float getTempC(const uint8_t* address) {
    const uint32_t start = micros();
    const float value = DallasTemperature::getTempC(address);
    readTime.add(micros() - start, isfinite(value) && value != DEVICE_DISCONNECTED_C);
    ++reads;
    return value;
  }
};

void asyncTemperatureBenchmarks() {
  if (!haveTemperature) {
    Serial.println(F("# SKIP async: no temperature sensor"));
    return;
  }
  for (uint8_t mode = 0; mode < 3; ++mode) {
    // Separate instances reset all discovery, request and fault state per phase.
    TimedTemperatureBus timedBus(&oneWire);
    AsyncMotorTemperature<TimedTemperatureBus> reader(timedBus);
    Stats pollBusy, pollIdle, pollStart, pollResult, resultInterval, resultAge, cycle;
    Serial.print(F("# ASYNC phase ")); Serial.print(mode);
    Serial.println(F(": see README for phase meaning"));
    Serial.flush();
    if (haveOled) display.update(0, 40, mode == 2 ? 100 : 0, 100, false);
    reader.begin();
    const uint32_t phaseStart = millis();
    uint32_t lastResult = 0;
    uint8_t results = 0, iteration = 0;
    while (results < 3 && millis() - phaseStart < 20000UL && Serial) {
      const uint32_t cycleStart = micros();
      const uint16_t beforeRequests = timedBus.requests, beforeReads = timedBus.reads, beforeProbes = timedBus.probes;
      const uint32_t pollStartUs = micros();
      const bool fresh = reader.poll();
      const uint32_t pollUs = micros() - pollStartUs;
      if (timedBus.reads != beforeReads) pollResult.add(pollUs, fresh);
      else if (timedBus.requests != beforeRequests) pollStart.add(pollUs, true);
      else if (timedBus.probes != beforeProbes) pollBusy.add(pollUs, true);
      else pollIdle.add(pollUs, !reader.faulted());
      if (fresh) {
        const uint32_t completed = micros();
        resultAge.add(completed - timedBus.lastRequest, true);
        if (results) resultInterval.add(completed - lastResult, true);
        lastResult = completed;
        measuredFloat = reader.value();
        ++results;
      }
      if (mode) {
        if (haveBmp) measuredFloat = bmp.readAltitude(ReferencePressure);
        measuredInt = analogRead(PotiPin);
        measuredInt = digitalRead(AutothrustPin);
        motor.write(MinimumMotorCommand);
        if (haveOled) display.update(0, 40, mode == 2 ? ((iteration & 1) ? 100 : 0) : 0, 100, false);
        delay(OriginalPauseMs);
      } else delay(10);
      cycle.add(micros() - cycleStart, true);
      ++iteration;
    }
    // Mode comments above identify each following summary group.
    summary(F("async_request_command"), timedBus.requestTime);
    summary(F("async_ready_probe"), timedBus.probeTime);
    summary(F("async_cached_read"), timedBus.readTime);
    summary(F("async_poll_busy"), pollBusy);
    summary(F("async_poll_idle"), pollIdle);
    summary(F("async_poll_start"), pollStart);
    summary(F("async_poll_result"), pollResult);
    summary(F("async_request_interval"), timedBus.requestInterval);
    summary(F("async_result_interval"), resultInterval);
    summary(F("async_request_to_result"), resultAge);
    summary(F("async_cycle_total"), cycle);
    Serial.print(F("# ASYNC valid results: ")); Serial.println(results);
    Serial.print(F("# ASYNC fault: ")); Serial.println(reader.faulted() ? 1 : 0);
    if (results < 3) Serial.println(F("# ASYNC incomplete: timeout/USB"));
    // Let a pending conversion finish before any subsequent sensor discovery.
    delay(800);
  }
}
#endif

// Representative live-like cycles, NOT the full flight controller. No mock
// sensor timings: they really read sensors and wait/convert like the old loop.
void cycleBenchmarks() {
  if (!haveBmp || !haveTemperature || !haveOled) {
    Serial.println(F("# SKIP cycles: device missing"));
    return;
  }
  for (uint8_t mode = 0; mode < 2; ++mode) {
    display.update(0, 40, mode ? 100 : 0, 100, false);
    cycleMode = mode;
    cycleIndex = 0;
    bench(mode == 0 ? F("cycle_static_display") : F("cycle_changing_display"), [](){
      const float altitude = bmp.readAltitude(ReferencePressure);
      measuredInt = analogRead(PotiPin);
      measuredInt = digitalRead(AutothrustPin);
      motor.write(MinimumMotorCommand);
      display.update(0, 40, cycleMode ? ((cycleIndex & 1) ? 100 : 0) : 0, 100, false);
      bool valid = isfinite(altitude);
      if (cycleIndex % 4 == 0) {
        sensors.requestTemperatures();
        measuredFloat = sensors.getTempCByIndex(0);
        valid = valid && measuredFloat != DEVICE_DISCONNECTED_C;
      } else delay(OriginalPauseMs);
      ++cycleIndex;
      return valid;
    });
  }
}

void runMeasurements() {
  Serial.println(F("# START. Switch trigger"));
  Serial.println(F("# HardwareTiming v2 / async + baseline"));
  Serial.println(F("# us; I2C=100kHz; IRQ/Servo on"));
  Serial.println(F("# raw;metric;sample;duration_us;valid"));
  Serial.println(F("# summary;metric;count;min_us;mean_us;max_us;errors"));
  haveOled = probe(OledAddress);
  Serial.print(F("# OLED ACK: ")); Serial.println(haveOled ? 1 : 0);
  const uint32_t bmpStart = micros();
  haveBmp = bmp.begin(BMP280_ADDRESS_ALT);
  const uint32_t bmpElapsed = micros() - bmpStart;
  Serial.print(F("raw;bmp_begin;1;")); Serial.print(bmpElapsed); Serial.print(';'); Serial.println(haveBmp ? 1 : 0);
  const uint32_t temperatureStart = micros();
  sensors.begin();
  sensors.setWaitForConversion(true); // old benchmark stays blocking on reruns
  const uint32_t temperatureElapsed = micros() - temperatureStart;
  haveTemperature = sensors.getDeviceCount() > 0;
  Serial.print(F("raw;onewire_begin;1;")); Serial.print(temperatureElapsed); Serial.print(';'); Serial.println(haveTemperature ? 1 : 0);
  Serial.print(F("# OneWire devices: ")); Serial.println(sensors.getDeviceCount());
  Serial.print(F("# OneWire maximum resolution bits: ")); Serial.println(sensors.getResolution());
  Serial.print(F("# OneWire parasite power: ")); Serial.println(sensors.isParasitePowerMode() ? 1 : 0);
  componentBenchmarks();
  displayBenchmarks();
#if LEGACY_BASELINE
  cycleBenchmarks();
#endif
#if !LEGACY_BASELINE
  asyncTemperatureBenchmarks();
#endif
  Serial.println(F("# DONE. Switch OFF/ON to repeat."));
}

void setup() {
  motor.attach(MotorPin);
  motor.write(MinimumMotorCommand);
  pinMode(AutothrustPin, INPUT);
  Wire.begin(); Wire.setClock(I2cClock);
  Wire.setWireTimeout(25000, true); // prevent a broken I2C bus hanging this diagnostic
  Serial.begin(115200);
}

void loop() {
  if (!Serial) { reported = false; delay(10); return; }
  if (!reported) {
    // An already enabled switch must not start a run when USB is opened.
    switchRaw = switchStable = digitalRead(AutothrustPin) != 0;
    switchArmed = !switchStable;
    switchChangedAt = millis();
    Serial.println(F("# READY. Switch OFF then ON."));
    reported = true;
  }
  const bool raw = digitalRead(AutothrustPin) != 0;
  if (raw != switchRaw) {
    switchRaw = raw;
    switchChangedAt = millis();
  }
  if (raw != switchStable && millis() - switchChangedAt >= 50) {
    switchStable = raw;
    if (!switchStable) switchArmed = true;
    else if (switchArmed) {
      switchArmed = false;
      runMeasurements(); // single run; debounce/USB waiting is not timed
    }
  }
  while (Serial.available()) Serial.read(); // no serial command starts a run
  delay(1);
}
