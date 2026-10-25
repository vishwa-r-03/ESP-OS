#pragma once

#if defined(BOARD_ESP32S3_N16R8)
    #include "board/boardEsp32s3N16r8.h"
#else
    #error "No board selected. Set -DBOARD_xxx in platformio.ini"
#endif

namespace config {
    constexpr const char *osName    = "ORA-OS";
    constexpr const char *osVersion = "0.0.1";
}