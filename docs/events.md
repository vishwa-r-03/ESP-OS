# OraOS Event Bus (`eventBus` & `events`)

This document explains what the OraOS event bus does, how its main parts work, and why the design uses C++ types and FreeRTOS queues. It focuses on the architecture and the important implementation trade-offs.

> **Note:** Code snippets are representative examples. Check the actual OraOS headers and implementation for exact names, signatures, and error-handling behavior.

## 1. What Is an Event?

An **event** is a small message describing something that happened or a state change—for example, an SD card was inserted, Wi-Fi connected, or the volume changed.

In this design, an event contains:

```cpp
struct event {
    eventType type;
    uint32_t  arg0;
    uint32_t  arg1;
};
```

- `type` identifies the event.
- `arg0` and `arg1` carry optional event-specific data. Their meaning should be documented for each event type.

Keeping events small makes them inexpensive to copy through FreeRTOS queues and avoids allocating a separate object for every message. Avoid large payloads or pointers unless their ownership and lifetime are clearly defined.

A compile-time size guard can protect the memory budget:

```cpp
static_assert(sizeof(event) <= 16,
              "event struct exceeds 16 bytes");
```

The 16-byte limit is a project choice. The actual size depends on field types, alignment, and compiler layout; `sizeof(event)` gives the size for the current build.

## 2. Why an Event Bus?

Without an event bus, the SD card driver might directly call the music player, file manager, and status bar whenever a card is inserted. That creates **tight coupling**: the driver must know about each component, and adding another listener requires changing the driver.

The event bus uses the **publish–subscribe (Pub–Sub)** pattern instead:

- **Publisher:** announces that an event occurred.
- **Event bus:** checks which subscriptions match the event.
- **Subscriber:** receives the event through its own queue and handles it when ready.

```text
[SD Card Driver]
       |
       v
   [Event Bus]
       |
       +------> [Queue A] ---> [UI Task]
       +------> [Queue B] ---> [Music Task]
       +------> [Queue C] ---> [File Manager]
```

Think of the bus as a postal sorting office: the sender drops off a message, and the sorting office routes copies to interested recipients. The sender does not need to know who those recipients are.

This makes subsystems easier to test and extend. It does not automatically guarantee delivery or make every call non-blocking; those behaviors depend on the implementation.

## 3. Event Types: Why `enum class`?

Event categories can be represented with a scoped enum:

```cpp
enum class eventType : uint8_t {
    none = 0,
    bootComplete,
    sdCardInserted,
    sdCardRemoved,
    wifiConnected,
    volumeChanged
};
```

Use the actual list from OraOS in the real header.

`enum class` is useful because:

- **Scoped names:** `events::eventType::sdCardInserted` reduces name collisions.
- **Type safety:** values do not implicitly convert to integers or mix freely with other enum types.
- **Controlled storage:** `: uint8_t` specifies a one-byte underlying type.

The enum is only one field of the event; the whole structure is larger. `none` should also have a clearly documented meaning and is not the same thing as an empty queue.

## 4. How the Event Bus Is Organized

The implementation typically contains a subscription table, synchronization state, and operations such as `init`, `createMailbox`, `subscribe`, `unsubscribe`, and `publish`.

### Fixed-size subscription table

A representative declaration is:

```cpp
subscription table[config::maxSubscriptions];
```

A fixed-size table gives predictable memory use and a bounded lookup time. It avoids dynamically growing a container at runtime, but it has a hard capacity: the implementation must handle attempts to subscribe when the table is full.

This does **not** mean the entire event system is heap-free. APIs such as `xQueueCreate()` and `xSemaphoreCreateMutex()` normally allocate FreeRTOS objects dynamically unless static-allocation APIs are used.

### Unnamed namespace

Private implementation details can live in an unnamed namespace inside `eventBus.cpp`:

```cpp
namespace {
    subscription table[config::maxSubscriptions];
    SemaphoreHandle_t tableLock = nullptr;
    uint32_t dropped = 0;

    bool isValidType(events::eventType type) {
        return type != events::eventType::none;
    }
}
```

This is illustrative code. Names in an unnamed namespace have **internal linkage**, so other source files cannot access them through ordinary external references. It keeps the table, lock, counter, and helper functions out of the public API. It is similar in purpose to encapsulation, though it is not literally a class's `private` section.

## 5. Queues, Subscriptions, and Event Flow

A subscriber creates a FreeRTOS queue, then subscribes that queue to an event type:

```cpp
QueueHandle_t mailbox = eventBus::createMailbox(10);

if (mailbox != nullptr) {
    eventBus::subscribe(
        mailbox,
        events::eventType::sdCardInserted
    );
}
```

The queue capacity (`10` here) is the maximum number of event items waiting to be received. Queue creation and subscription results should be checked according to the actual API.

When a storage task detects an insertion, it publishes an event:

```cpp
events::event ev{
    events::eventType::sdCardInserted,
    0,
    0
};

eventBus::publish(ev);
```

The bus checks matching subscriptions and attempts to send a copy to each subscriber's queue. A subscriber task can wait for and process messages:

```cpp
events::event ev;

if (xQueueReceive(mailbox, &ev, portMAX_DELAY) == pdTRUE) {
    // Handle ev.type and interpret arg0/arg1.
}
```

`portMAX_DELAY` normally allows the receiving task to wait indefinitely for an item, subject to FreeRTOS configuration. This blocks the subscriber task, not the publisher.

```text
Detect change -> Build event -> Publish
                               |
                               v
                      Find subscriptions
                               |
                               v
                    Try each matching queue
                               |
                               v
                     Subscriber handles it
```

## 6. Concurrency and Mutex Protection

Multiple FreeRTOS tasks may subscribe, unsubscribe, or publish at the same time. Shared subscription-table access must therefore be synchronized.

A typical mutex pattern is:

```cpp
if (xSemaphoreTake(tableLock, portMAX_DELAY) != pdTRUE) {
    return false;
}

// Read or update the protected table.

xSemaphoreGive(tableLock);
return result;
```

The real implementation must ensure that every path after successfully acquiring the mutex releases it. A single cleanup point can make this easier to review, but a **single-exit pattern does not guarantee deadlock prevention**. Deadlocks can still result from lock ordering, blocking while holding a lock, or overlooked error paths.

Keep critical sections short. Also define what happens if a queue is unsubscribed or deleted while a publish operation is using its handle; protecting the table alone does not automatically solve every queue-lifetime race.

## 7. Non-Blocking Publishing and Dropped Events

A zero-timeout queue send does not wait for queue space:

```cpp
if (xQueueSend(sub.mailbox, &e, 0) == pdTRUE) {
    delivered++;
} else {
    dropped++;
}
```

If a subscriber queue is full, the send fails immediately and the publisher can continue to other subscribers. This reduces the risk that a slow consumer stalls the publisher.

However, **non-blocking does not mean guaranteed delivery**. A full queue can cause an event delivery to be dropped. If an event is critical, consider a larger queue, coalescing repeated status updates, a retry/acknowledgement design, or a way for consumers to recover the latest state.

Define what `dropped` counts. If one event is sent to three subscribers and two queue sends fail, that is normally two **dropped deliveries**, not two distinct published events.

### Reading the dropped counter safely

Do not assume that reading a naturally aligned 32-bit integer is safe merely because the ESP32 is a 32-bit processor. A single hardware load may avoid a torn read, but concurrent access to an ordinary C++ variable can still be a data race.

Protect the counter with appropriate synchronization or use a suitable atomic mechanism supported by the toolchain. If the counter is accessed from an ISR, use only ISR-safe mechanisms. A normal FreeRTOS mutex must not be used in an ISR. Also remember that a `uint32_t` counter eventually wraps around.

## 8. Initialization, ISRs, and UI Safety

Initialize the event bus before tasks use it:

```cpp
eventBus::init();
```

Define how initialization failure is reported and whether repeated initialization is allowed.

A typical event bus using a mutex and `xQueueSend()` should be treated as **task-context-only** unless the source explicitly provides an ISR-safe path. Do not call ordinary task-context queue or mutex APIs from an interrupt service routine.

A safer flow is:

```text
Hardware ISR
    |
    v
Minimal ISR work
    |
    v
ISR-safe notification / queue operation
    |
    v
Service task publishes event
    |
    v
Event bus routes to subscriber queues
```

This pattern also helps UI safety. For example, a storage task can publish an SD-card event and the UI task can update its widgets when it receives it. The event bus does not itself make UI calls thread-safe: UI updates must still happen in the appropriate task context for the framework and version being used.

## 9. Main Trade-Offs

| Design choice | Benefit | Limitation |
|---|---|---|
| Small fixed-size events | Fast, predictable copies | Limited payload |
| `enum class` | Strong typing and scoped names | Explicit casts needed for numeric conversions |
| Fixed-size table | Predictable RAM and bounded scan | Capacity limit |
| Queue per subscriber | Consumers work independently | RAM use; queues can fill |
| Zero-timeout sends | Publisher does not wait for queue space | Deliveries may be dropped |
| Mutex-protected table | Coordinates shared access | Locking overhead and lifetime complexity |
| Dropped counter | Helps diagnose overload | Must be safely synchronized and clearly defined |

The implementation should also define duplicate-subscription behavior, invalid-event handling, table-full behavior, queue ownership, unsubscribe semantics, and which functions are safe to call from an ISR.

## 10. Checklist for Adding an Event

- [ ] Add the event type to the central enum.
- [ ] Document `arg0` and `arg1` meanings, ranges, and units.
- [ ] Publish the event from the subsystem that owns the state change.
- [ ] Subscribe only components that need it.
- [ ] Check queue-creation and subscription failures.
- [ ] Decide whether a delivery may be dropped if a queue is full.
- [ ] Confirm queue capacity and mailbox lifetime.
- [ ] Handle the event in the correct task context.
- [ ] Keep ISR code separate from task-only event-bus APIs.
- [ ] Update this document when API behavior or delivery guarantees change.

## Glossary

- **Event:** A message describing a state change or occurrence.
- **Publisher:** Component that sends an event.
- **Subscriber:** Component that registers to receive an event type.
- **Event bus:** Routes events to matching subscribers.
- **Mailbox:** A subscriber's FreeRTOS queue.
- **Mutex:** Synchronization primitive for shared resources.
- **ISR:** Interrupt Service Routine, subject to stricter API and timing rules.
- **`static_assert`:** Compile-time check that fails the build if a condition is false.
- **Internal linkage:** Limits a name's linkage to one translation unit.

## Summary

The OraOS event bus lets independent subsystems communicate without hard-coding calls to one another. Publishers create compact event records; the bus checks the subscription table and attempts to copy each event into matching FreeRTOS queues. The design favors predictable table memory, typed event categories, and non-blocking queue sends.

Its reliability still depends on careful initialization, synchronization, queue sizing, mailbox lifetime, and overload handling. In particular, a full queue can cause a delivery to be lost, concurrent diagnostic access must be synchronized correctly, and ordinary task-context APIs must not be called directly from an ISR.