// Run the production async reader with a fake bus and a controllable clock.
// No sensor required. Pin 9 stays at the existing minimum motor command.
#include <Servo.h>
#include "MotorTemperature.h"
unsigned long nowMs = 0;
unsigned long clockForTest() { return nowMs; }
Servo motor;
uint8_t checks = 0, failures = 0;

struct FakeBus {
  bool present = true, parasite = false, ready = false, waits = true;
  float reading = 40;
  uint8_t requests = 0, polls = 0, discoveries = 0;
  void begin() {}
  void setWaitForConversion(bool wait) { waits = wait; }
  bool getAddress(uint8_t* address, uint8_t) { ++discoveries; address[0] = 0x28; return present; }
  bool isParasitePowerMode() { return parasite; }
  uint8_t getResolution(const uint8_t*) { return 12; }
  int16_t millisToWaitForConversion(uint8_t) { return 750; }
  void requestTemperatures() { ++requests; }
  bool isConversionComplete() { ++polls; return ready; }
  float getTempC(const uint8_t*) { return reading; }
};
using Reader = AsyncMotorTemperature<FakeBus, clockForTest>;

void check(bool condition, const __FlashStringHelper* label) {
  ++checks;
  if (!condition) { ++failures; Serial.print(F("FAIL: ")); Serial.println(label); }
}

void runChecks() {
  FakeBus bus;
  Reader reader(bus);
  nowMs = 0; reader.begin();
  check(!bus.waits && bus.requests == 1, F("asynchronous immediate start"));
  check(!reader.hasValue(), F("no synthetic first value"));
  nowMs = 100;
  check(!reader.poll() && !reader.hasValue(), F("busy is not a temperature"));
  nowMs = 612; bus.ready = true;
  check(reader.poll() && reader.value() == 40, F("first completed sample"));
  nowMs = 4999; bus.ready = false; reader.poll();
  check(bus.requests == 1, F("no early request"));
  nowMs = 5000; reader.poll();
  check(bus.requests == 2, F("request every 5000 ms"));
  nowMs = 5612; bus.ready = true; bus.reading = 90;
  check(reader.poll() && reader.value() == 90, F("new real value replaces old"));
  nowMs = 10000; reader.poll();
  nowMs = 10612; bus.reading = DEVICE_DISCONNECTED_C;
  check(!reader.poll() && reader.faulted(), F("disconnect rejected"));
  check(reader.hasValue() && reader.value() == 90, F("retain last valid on error"));
  nowMs = 15000; bus.ready = false; reader.poll();
  nowMs = 15999; reader.poll();
  check(bus.requests == 4, F("timeout does not busy-retry"));
  nowMs = 16000; reader.poll();
  check(reader.faulted() && reader.value() == 90, F("busy timeout retains value"));
  nowMs = 20000; reader.poll();
  nowMs = 20612; bus.ready = true; bus.reading = 35;
  check(reader.poll() && !reader.faulted() && reader.value() == 35, F("recovery at regular request"));
  check(bus.discoveries == 1, F("cached ROM avoids repeated search"));

  FakeBus absent; absent.present = false;
  Reader missing(absent); missing.begin();
  check(missing.faulted() && !missing.hasValue(), F("absent at boot has no valid value"));
  check(!missing.poll() && absent.requests == 0, F("absent sensor cannot inject a value"));

  FakeBus parasite; parasite.parasite = true;
  Reader powered(parasite);
  nowMs = 0; powered.begin();
  nowMs = 749;
  check(!powered.poll(), F("parasite waits for maximum conversion time"));
  nowMs = 750;
  check(powered.poll() && parasite.polls == 0, F("no parasite busy read slots"));

  FakeBus wrapped;
  Reader rollover(wrapped);
  nowMs = UINT32_MAX - 100UL; rollover.begin();
  nowMs = 50; rollover.poll();
  check(wrapped.requests == 1 && !rollover.faulted(), F("rollover elapsed time"));
  nowMs = 700; wrapped.ready = true;
  check(rollover.poll(), F("sample across millis rollover"));
  nowMs = 4898; rollover.poll();
  check(wrapped.requests == 1, F("no early request across rollover"));
  nowMs = 4899; rollover.poll();
  check(wrapped.requests == 2, F("request deadline across rollover"));
  Serial.print(F("CHECKS=")); Serial.println(checks);
  Serial.print(F("FAILURES=")); Serial.println(failures);
  Serial.println(F("# DONE."));
}

void setup() { motor.attach(9); motor.write(50); Serial.begin(115200); }
void loop() {
  static bool done = false;
  if (!Serial) { done = false; delay(10); return; }
  if (!done) { delay(300); runChecks(); done = true; }
}
