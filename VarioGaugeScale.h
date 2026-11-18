#pragma once
#include <stdint.h>

constexpr uint8_t VarioGaugeZero = 3; // three sink blocks, seven climb blocks
constexpr float VarioGaugeStep = 0.2f; // m/s per block

// Finite inputs, rounded to the nearest block and saturated at -0.6/+1.4 m/s.
constexpr uint8_t varioGaugePosition(float vario) {
  return vario <= -0.6f ? 0 : vario >= 1.4f ? 10 :
    uint8_t(VarioGaugeZero + int(vario * 5.0f + (vario >= 0 ? 0.5f : -0.5f)));
}

static_assert(varioGaugePosition(-0.6f) == 0, "Full sink: three blocks");
static_assert(varioGaugePosition(-0.2f) == 2, "One sink block");
static_assert(varioGaugePosition(0.0f) == VarioGaugeZero, "Zero: no filled blocks");
static_assert(varioGaugePosition(0.2f) == 4, "One climb block");
static_assert(varioGaugePosition(0.6f) == 6, "AT target: three climb blocks");
static_assert(varioGaugePosition(1.4f) == 10, "Full climb: seven blocks");
static_assert(varioGaugePosition(-9.9f) == 0 && varioGaugePosition(9.9f) == 10,
              "Saturate outside display range");
static_assert(varioGaugePosition(-0.09f) == 3 && varioGaugePosition(0.09f) == 3,
              "Near zero: empty");
static_assert(varioGaugePosition(-0.11f) == 2 && varioGaugePosition(0.11f) == 4,
              "Round in both directions");
