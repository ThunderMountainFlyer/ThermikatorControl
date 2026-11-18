/***************************************************************************
  This is a library for the BMP280 humidity, temperature & pressure sensor

  Designed specifically to work with the Adafruit BMEP280 Breakout 
  ----> http://www.adafruit.com/products/2651

  These sensors use I2C or SPI to communicate, 2 or 4 pins are required 
  to interface.

  Adafruit invests time and resources providing this open source code,
  please support Adafruit andopen-source hardware by purchasing products
  from Adafruit!

  Written by Limor Fried & Kevin Townsend for Adafruit Industries.  
  BSD license, all text above must be included in any redistribution
 ***************************************************************************/

#include <Wire.h>
#include <Adafruit_BMP280.h>
#include <DallasTemperature.h>
#include <Servo.h>
#include "OledGaugeDisplay.h"
#include "DisplayTestValues.h"
#include "UsbTestGate.h"
#include "MotorTemperature.h"
#include "VarioSampler.h"

// Existing Pro Micro/ATmega32U4 wiring and calibrated control settings.
#define I2C_ADDRESS 0x3C
#define POTI 18
#define ONE_WIRE_BUS 8
#define MaxPoti 980
#define MinPoti 370
#define MinPowerPWM 50
#define MaxPowerPWM 140
#define AutothrustPin 10
#define MaxMotorTemp 80
#define MaxClimbRate 0.6f
#define ClimbHysterese 0.2f
constexpr float AutothrustRate = 10.0f; // percentage points per second

Adafruit_BMP280 bmp;
VarioSampler varioSensor(bmp);
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
MotorTemperature motorTemperature(sensors);
Servo ServoPower;
OledGaugeDisplay display;

UsbRunMode usbRunMode = UsbRunMode::Detecting;
unsigned long displayTestStarted = 0;
bool displayTestActive = false, measurementsReady = false, historyReady = false;
uint32_t nextControlUs = 0, lastControlUs = 0, lastSampleUs = 0;
int batteryBeforeTest = 100, RemainingAkku = 100, Temperature = 0;
int PotiSetPower = 0, AllowedPower = 0, PowerPWM = MinPowerPWM;
float ReducePower = 0, VarioAverage = 0, previousHeightAverage = 0;
float heights[4] = {}, velocities[4] = {};

#ifdef VARIO_TIMING_BENCH
struct BenchStats {
  uint32_t count = 0, shortest = UINT32_MAX, longest = 0, sum = 0;
  void add(uint32_t value) {
    ++count; sum += value;
    if (value < shortest) shortest = value;
    if (value > longest) longest = value;
  }
};
BenchStats frameTimes, frameLateness, resultOffsets, controlTimes, displayChunks, tempPollTimes;
bool benchActive = false, benchFinished = false;
uint32_t benchStarted = 0, benchLastSample = 0, benchLastControl = 0;
void report(const __FlashStringHelper* label, const BenchStats& s) {
  Serial.print(label); Serial.print(';'); Serial.print(s.count); Serial.print(';');
  Serial.print(s.count ? s.shortest : 0); Serial.print(';');
  Serial.print(s.count ? s.sum / s.count : 0); Serial.print(';'); Serial.println(s.longest);
}
void finishBench() {
  Serial.println(F("# metric;count;min_us;mean_us;max_us"));
  report(F("vario_frame"), frameTimes); report(F("start_lateness"), frameLateness);
  report(F("result_offset"), resultOffsets); report(F("control_frame"), controlTimes);
  report(F("display_chunk"), displayChunks); report(F("temperature_poll"), tempPollTimes);
  Serial.print(F("missed_frames;")); Serial.println(varioSensor.missedFrames());
  Serial.print(F("sensor_errors;")); Serial.println(varioSensor.errors());
  Serial.print(F("temperature_valid;")); Serial.println(motorTemperature.hasValue() ? 1 : 0);
  Serial.print(F("temperature_fault;")); Serial.println(motorTemperature.faulted() ? 1 : 0);
  Serial.print(F("temperature_C;")); Serial.println(Temperature);
  Serial.println(F("# DONE."));
  benchFinished = true;
}
#endif

bool computerConnected() {
  return USBDevice.configured() && (USBSTA & _BV(VBUS));
}

void initializeMeasurements() {
  motorTemperature.begin();
  Temperature = 0;
  varioSensor.begin();
  historyReady = false;
  VarioAverage = ReducePower = 0;
  for (uint8_t i = 0; i < 4; ++i) velocities[i] = 0;
  // Readout is planned 80 ms after the first start at +100 ms. Control
  // follows at +200 ms, then every 800 ms, even if a sensor read fails.
  lastControlUs = micros() - uint32_t(VarioFrameUs);
  nextControlUs = micros() + 200000UL;
  measurementsReady = true;
}

void acceptAltitude() {
  const float altitude = varioSensor.altitude();
  const uint32_t sampleTime = varioSensor.sampleTimeUs();
  const uint32_t elapsed = sampleTime - lastSampleUs;
  if (!historyReady || elapsed > VarioFrameUs + VarioFrameUs / 2) {
    for (uint8_t i = 0; i < 4; ++i) { heights[i] = altitude; velocities[i] = 0; }
    previousHeightAverage = altitude;
    VarioAverage = 0;
    historyReady = true;
  } else {
    for (uint8_t i = 3; i > 0; --i) { heights[i] = heights[i-1]; velocities[i] = velocities[i-1]; }
    heights[0] = altitude;
    const float heightAverage = (heights[0] + heights[1] + heights[2] + heights[3]) * 0.25f;
    velocities[0] = elapsed ? (heightAverage - previousHeightAverage) * (1000000.0f / elapsed) : 0;
    previousHeightAverage = heightAverage;
    VarioAverage = (velocities[0] + velocities[1] + velocities[2] + velocities[3]) * 0.25f;
    VarioAverage = constrain(VarioAverage, -9.9f, 9.9f);
  }
  lastSampleUs = sampleTime;
#ifdef VARIO_TIMING_BENCH
  if (benchActive) {
    if (benchLastSample) frameTimes.add(sampleTime - benchLastSample);
    benchLastSample = sampleTime;
    frameLateness.add(varioSensor.latenessUs());
    resultOffsets.add(micros() - sampleTime);
  }
#endif
}

void control(bool testMode, uint32_t now) {
#ifdef VARIO_TIMING_BENCH
  if (benchActive) {
    if (benchLastControl) controlTimes.add(now - benchLastControl);
    benchLastControl = now;
  }
#endif
  if (!testMode) {
    // The motor controller provides its own ramp; use the current input directly.
    const int rawPoti = analogRead(POTI);
    const int poti = constrain(rawPoti, MinPoti, MaxPoti);
    PotiSetPower = map(poti, MinPoti, MaxPoti, 0, 100);
  }
  const bool autothrust = digitalRead(AutothrustPin) != 0;
  // 10 percentage points per second, equivalent to 8 per 800-ms frame.
  // No dependence on background/UI loop speed. Do not accumulate missed steps.
  const float seconds = min(now - lastControlUs, VarioFrameUs) * 0.000001f;
  lastControlUs = now;
  if (autothrust && (testMode || (varioSensor.valid() && historyReady))) {
    if (VarioAverage > MaxClimbRate + ClimbHysterese) ReducePower += AutothrustRate * seconds;
    if (VarioAverage < MaxClimbRate - ClimbHysterese) ReducePower -= AutothrustRate * seconds;
    ReducePower = constrain(ReducePower, 0.0f, 100.0f);
  }
  AllowedPower = max(0, PotiSetPower - (autothrust ? int(ReducePower + 0.5f) : 0));
  if (Temperature > MaxMotorTemp) {
    const int limit = max(20, 100 - (Temperature - MaxMotorTemp) * 10);
    AllowedPower = min(AllowedPower, limit);
  }
  if (!testMode && !motorTemperature.hasValue()) AllowedPower = 0;
  PowerPWM = map(AllowedPower, 0, 100, MinPowerPWM, MaxPowerPWM);
#ifdef VARIO_TIMING_BENCH
  ServoPower.write(MinPowerPWM); // bench firmware can NEVER drive requested power
#else
  ServoPower.write(testMode || !motorTemperature.hasValue() ? MinPowerPWM : PowerPWM);
#endif
}

void setup() {
  ServoPower.attach(9); ServoPower.write(MinPowerPWM);
  pinMode(AutothrustPin, INPUT);
  Wire.begin(); Wire.setClock(100000L);
  Wire.setWireTimeout(5000, true);
  display.begin(I2C_ADDRESS);
#ifdef VARIO_TIMING_BENCH
  Serial.begin(115200);
#endif
}

void loop() {
#ifdef VARIO_TIMING_BENCH
  const bool testMode = false; // real sensors, but motor is compile-time inhibited
  if (benchFinished) { delay(1); return; }
  // dtr() is non-blocking, unlike Serial_::operator bool() in this AVR core.
  if (!benchActive && Serial.dtr()) { benchActive = true; benchStarted = millis(); }
#else
  // The complete version image and USB detection share one 1-second window.
  // Keep the motor at minimum until it ends, even if USB is recognized early.
  if (!display.startupComplete()) {
    display.service();
    if (display.startupReady())
      usbRunMode = nextUsbRunMode(usbRunMode, display.startupElapsedMs(), computerConnected());
    delay(1); return;
  }
  usbRunMode = nextUsbRunMode(usbRunMode, display.startupElapsedMs(), computerConnected());
  const bool testMode = usbRunMode == UsbRunMode::Test;
#endif
  if (testMode) {
    if (!displayTestActive) {
      displayTestStarted = millis(); batteryBeforeTest = RemainingAkku;
      ReducePower = 0; displayTestActive = true;
      nextControlUs = micros(); lastControlUs = nextControlUs - VarioFrameUs;
    }
    measurementsReady = false;
    const DisplayTestValues test = displayTestValues(millis() - displayTestStarted);
    Temperature = test.temperature; RemainingAkku = test.battery;
    PotiSetPower = test.power; VarioAverage = test.vario;
  } else {
    if (displayTestActive) RemainingAkku = batteryBeforeTest;
    displayTestActive = false;
    if (!measurementsReady) initializeMeasurements();
    // Highest priority: trigger/read pressure on phase-locked deadlines.
    if (varioSensor.poll()) acceptAltitude();
    if (!varioSensor.valid()) historyReady = false;
  }
  uint32_t now = micros();
  if (deadlineReached(now, nextControlUs)) {
    nextControlUs = followingFrame(nextControlUs, now);
    control(testMode, now);
  }
  uint32_t spare = remainingTime(micros(), nextControlUs);
  if (!testMode) spare = min(spare, varioSensor.spareUs());
  // Cached OneWire reads measured ~12.5 ms: reserve 50 ms, including margin.
  if (!testMode && spare >= 50000UL) {
#ifdef VARIO_TIMING_BENCH
    const uint32_t tempStart = micros();
#endif
    if (motorTemperature.poll()) Temperature = motorTemperature.value();
#ifdef VARIO_TIMING_BENCH
    if (benchActive) tempPollTimes.add(micros() - tempStart);
#endif
  }
#ifdef VARIO_TIMING_BENCH
  // Stress redraws and warning blink while measuring real pressure/temperature.
  display.update((micros() / VarioFrameUs & 1) ? 2 : -2, 85,
                 (micros() / VarioFrameUs & 1) ? 100 : 0, 20, true);
#else
  display.update(VarioAverage, Temperature, AllowedPower, RemainingAkku, digitalRead(AutothrustPin) != 0);
#endif
  spare = remainingTime(micros(), nextControlUs);
  if (!testMode) spare = min(spare, varioSensor.spareUs());
  if (spare >= 30000UL) {
#ifdef VARIO_TIMING_BENCH
    const uint32_t chunkStart = micros();
#endif
    display.service();
#ifdef VARIO_TIMING_BENCH
    if (benchActive) displayChunks.add(micros() - chunkStart);
#endif
  }
#ifdef VARIO_TIMING_BENCH
  if (benchActive && (frameTimes.count >= 24 || millis() - benchStarted > 30000UL)) finishBench();
#endif
  delay(1); // cooperative idle, not the old 638-ms blocking pause
}
