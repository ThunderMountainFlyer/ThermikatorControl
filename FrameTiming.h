#pragma once
#include <stdint.h>

constexpr uint32_t VarioFrameUs = 800000UL;
constexpr uint32_t VarioReadDelayUs = 80000UL;
// Signed differences handle micros() wrap for deadlines < 2^31 us away.
constexpr bool deadlineReached(uint32_t now, uint32_t deadline) {
  return int32_t(now - deadline) >= 0;
}
constexpr uint32_t remainingTime(uint32_t now, uint32_t deadline) {
  return deadlineReached(now, deadline) ? 0 : deadline - now;
}
// Advance on the original phase; skip missed slots rather than burst catch-up.
constexpr uint32_t followingFrame(uint32_t deadline, uint32_t now) {
  return deadline + ((now - deadline) / VarioFrameUs + 1) * VarioFrameUs;
}
static_assert(!deadlineReached(999, 1000), "Not due early");
static_assert(deadlineReached(1000, 1000), "Due at deadline");
static_assert(remainingTime(100, 1000) == 900, "Correct spare budget");
static_assert(deadlineReached(20, UINT32_MAX - 10), "Deadline across wrap");
static_assert(!deadlineReached(UINT32_MAX - 10, 20), "Future deadline across wrap");
static_assert(followingFrame(100, 110) == 800100UL, "Keep phase after small delay");
static_assert(followingFrame(100, 1600110) == 2400100UL, "Skip overdue slots");
