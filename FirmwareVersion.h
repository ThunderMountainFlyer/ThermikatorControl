#pragma once
#include <stdint.h>

// Release tags must match this value: vMAJOR.MINOR.PATCH.
#define FIRMWARE_VERSION "2.0.0"

// Shared version-display and USB-detection window, after the full image is sent.
constexpr uint32_t FirmwareStartupWindowMs = 1000;
