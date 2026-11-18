#pragma once
#include <stdint.h>
#include "VarioGaugeScale.h"

// Synthetic controller inputs for USB bench testing; no physical motor output.
struct DisplayTestValues {
  float vario;
  int temperature;
  int power;
  int battery;
};

inline DisplayTestValues displayTestValues(uint32_t elapsedMs) {
  // A 27.5-second cycle. Keep the final values for at least 2.5 seconds
  // so a completely empty battery bar can also be seen flashing.
  const uint8_t step = (elapsedMs / 250UL) % 110;
  const uint8_t heat = step < 70 ? step : 70;
  const uint8_t discharge = step < 100 ? step : 100;
  // Hold full requested power long enough to observe AT and thermal limiting.
  const uint8_t powerPhase = step % 30;
  const int powerSteps = powerPhase <= 10 ? powerPhase :
    (powerPhase < 20 ? 10 : 30 - powerPhase);
  const uint8_t triangle = step % 40;
  // Zero -> +1.4 -> zero -> -0.6 -> zero, with holds at both endpoints.
  const int varioSteps = triangle <= 7 ? triangle :
    (triangle < 17 ? 7 : (triangle <= 27 ? 24 - int(triangle) :
    (triangle < 37 ? -3 : int(triangle) - 40)));
  return {varioSteps * VarioGaugeStep, 20 + int(heat), powerSteps * 10, 100 - int(discharge)};
}
