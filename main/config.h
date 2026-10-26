#pragma once
#include <cstdint>

#if defined(BOARD_ESP32S3_N16R8)
    #include "board/boardEsp32s3N16r8.h"
#else
    #error "No board selected. Set -DBOARD_xxx in main/CMakeLists.txt"
#endif

namespace config {
    constexpr const char *osName    = "ORA-OS";
    constexpr const char *osVersion = "0.0.1";
    constexpr uint8_t maxSubscriptions   = 32;
    constexpr uint8_t defaultMailboxDepth = 8;
}