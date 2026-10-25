#pragma once

// ESP32-S3 N16R8 dev board: 16 MB flash, 8 MB octal PSRAM.
//
// GPIOs to avoid when assigning pins later:
//   26-32  : internal flash
//   33-37  : octal PSRAM
//   19, 20 : native USB
//   0, 3, 45, 46 : strapping pins (usable, but with care)

namespace board {
    constexpr const char *name = "esp32-s3 n16r8";

    // capability flags: apps and services check these, never the chip name
    constexpr bool hasPsram     = true;
    constexpr bool hasBle       = true;
    constexpr bool hasBtClassic = false;

    // pins are added here as each peripheral comes online
}