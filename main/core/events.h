#pragma once
#include <cstdint>

namespace events {

    enum class eventType : uint8_t {
        none = 0,
        bootComplete,       // arg0: unused, arg1: unused
        sdCardInserted,     // arg0: storage slot/port, arg1: unused
        sdCardRemoved,      // arg0: storage slot/port, arg1: unused
        wifiStateChanged,   // arg0: 0 = disconnected, 1 = connected, arg1: error code or status flags
        volumeChanged,      // arg0: new volume level (0-100), arg1: stream type (0 = master, 1 = media)

        count               // always last: total number of event types
    };

    struct event {
        eventType type;
        uint32_t  arg0;
        uint32_t  arg1;
    };

    // queues copy this struct on every send, so it must stay small
    static_assert(sizeof(event) <= 16, "event struct exceeds 16 bytes");

} // namespace events