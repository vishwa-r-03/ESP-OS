#pragma once
#include "events.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

namespace eventBus {

    // safe to call more than once
    bool init();

    // creates a queue whose item size matches events::event exactly
    QueueHandle_t createMailbox(uint8_t depth);

    // subscribing twice to the same type is allowed and has no extra effect
    bool subscribe(QueueHandle_t mailbox, events::eventType type);
    bool unsubscribe(QueueHandle_t mailbox, events::eventType type);

    // never blocks, returns how many mailboxes received the event
    uint8_t publish(const events::event &e);

    // events lost because a mailbox was full (informational, read without the lock)
    uint32_t droppedCount();

} // namespace eventBus