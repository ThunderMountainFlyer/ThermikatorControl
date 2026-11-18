#pragma once
#include <stdint.h>
#include "FirmwareVersion.h"

enum class UsbRunMode : uint8_t { Detecting, Test, Live };
constexpr uint32_t UsbDetectionWindowMs = FirmwareStartupWindowMs;

// Live is a one-way latch. Only a CPU reset can reopen startup detection.
constexpr UsbRunMode nextUsbRunMode(UsbRunMode mode, uint32_t elapsedMs, bool host) {
  return mode == UsbRunMode::Live ? UsbRunMode::Live :
    mode == UsbRunMode::Test ? (host ? UsbRunMode::Test : UsbRunMode::Live) :
    elapsedMs >= UsbDetectionWindowMs ? UsbRunMode::Live :
    host ? UsbRunMode::Test : UsbRunMode::Detecting;
}

// Verify boundaries and the live latch at compile time, without runtime cost.
static_assert(nextUsbRunMode(UsbRunMode::Detecting, 0, false) == UsbRunMode::Detecting, "Wait at boot");
static_assert(nextUsbRunMode(UsbRunMode::Detecting, 999, false) == UsbRunMode::Detecting, "Wait until deadline");
static_assert(nextUsbRunMode(UsbRunMode::Detecting, 999, true) == UsbRunMode::Test, "Accept host inside window");
static_assert(nextUsbRunMode(UsbRunMode::Detecting, 1000, true) == UsbRunMode::Live, "Reject host at deadline");
static_assert(nextUsbRunMode(UsbRunMode::Detecting, 1000, false) == UsbRunMode::Live, "Timeout to live");
static_assert(nextUsbRunMode(UsbRunMode::Test, 2000, true) == UsbRunMode::Test, "Keep selected test mode");
static_assert(nextUsbRunMode(UsbRunMode::Test, 2000, false) == UsbRunMode::Live, "Disconnect to live");
static_assert(nextUsbRunMode(UsbRunMode::Live, 0, true) == UsbRunMode::Live, "No reentry after disconnect");
static_assert(nextUsbRunMode(UsbRunMode::Live, UINT32_MAX, true) == UsbRunMode::Live, "No reentry before timer wrap");
static_assert(nextUsbRunMode(UsbRunMode::Live, 0, false) == UsbRunMode::Live, "No reentry after timer wrap");
