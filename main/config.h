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

    constexpr uint8_t maxManagedTasks = 16;

    // one space to see how important each class of task is

    namespace prio{
        constexpr uint8_t background = 2;  //logging, housekeeping, etc
        constexpr uint8_t service    = 5;  //storage, network, etc
        constexpr uint8_t ui         = 8;  //LVGL
        constexpr uint8_t audio      = 10; //ISR, etc
    }

    //bytes, not words, for ESP-IDF
    namespace stack{
        constexpr uint32_t small     = 3072;
        constexpr uint32_t normal    = 4096;
        constexpr uint32_t large     = 8192;
    }
}