# Events: how the parts of ORA-OS talk to each other

> **Files covered:** `main/core/events.h`, `main/core/eventBus.h`, `main/core/eventBus.cpp`
> **Level:** beginner-friendly. No prior OS or RTOS knowledge assumed.
> **You will learn:** what a task is, why shared data is dangerous, what a queue and a mutex do, what publish/subscribe means, and why each line of the event bus is written the way it is.

---

## 1. The problem we are solving

ORA-OS will have many independent pieces:

- an **SD card driver** that notices when a card is inserted or removed
- a **file manager app** that needs to refresh its list when that happens
- a **music player** that must stop playing if the card is pulled out
- a **status bar** that shows an SD icon
- a **Wi-Fi service** that announces "connected" or "disconnected"

The naive way to wire this up is to let the SD driver call each of them directly:

```cpp
// BAD: the driver must know about every app that cares
void sdDriver_onCardInserted() {
    fileManager_refresh();
    musicPlayer_rescan();
    statusBar_showSdIcon();
}
```

This looks fine with three modules and falls apart quickly:

| Problem | What goes wrong |
|---|---|
| **Tight coupling** | The driver includes the headers of every app. Adding a new app means editing the driver. |
| **Wrong thread** | The driver runs in *its* task, but it just called UI code that must only run in the *UI* task. This causes crashes. |
| **Blocking** | If `musicPlayer_rescan()` takes 2 seconds, the driver freezes for 2 seconds and misses the next card event. |

We need a way for a module to say **"something happened"** without knowing or caring who is listening. That is what the event bus does.

---

## 2. The idea in one analogy

Think of a **newspaper delivery service**.

- The **newspaper** announces news (an *event*). It does not know who its readers are.
- Readers **subscribe** to the sections they care about: sports, weather, politics.
- Each reader has their own **mailbox** at home. The delivery person drops a *copy* of the paper into it.
- Readers read **when they have time**. The newspaper never waits for anyone to finish reading.
- If a reader's mailbox is full because they never empty it, the delivery person **skips that house** and moves on. Everyone else still gets their paper.

Now map it to the code:

| Newspaper world | ORA-OS code |
|---|---|
| A news story | `events::event` |
| A section (sports, weather) | `events::eventType` |
| Subscribing | `eventBus::subscribe(mailbox, type)` |
| Home mailbox | a FreeRTOS **queue** created by `eventBus::createMailbox()` |
| The delivery person | `eventBus::publish(e)` |
| Subscriber list at the delivery office | the `table[]` inside `eventBus.cpp` |
| Delivery person skipping a full mailbox | `dropped++` |
| The key to the subscriber-list room | the mutex `tableLock` |

This pattern is called **publish/subscribe** (pub/sub).

---

## 3. A few background concepts

### 3.1 What is a task?

A **task** is a function with its own private **stack** that runs an endless loop. FreeRTOS (the scheduler) switches between tasks so quickly that they appear to run at the same time. On the ESP32-S3 two tasks really can run at the same time, one on each core.

A task is usually **blocked**, which means asleep and using no CPU, until something it is waiting for happens. An OS is mostly many tasks sleeping until they have work.

### 3.2 Why sharing data between tasks is dangerous

Imagine two tasks both doing `counter = counter + 1`. That single line is really three steps: read, add, write.

```
Task A reads counter (5)
                          Task B reads counter (5)
Task A writes 6
                          Task B writes 6      <- should be 7!
```

The scheduler can pause a task *between any two steps*. This is a **race condition**, and it produces bugs that happen once an hour and vanish when you add a `printf`.

There are only two safe strategies:

1. **Locks (mutexes):** only one task at a time may touch the data.
2. **Message passing:** tasks never share data. They send **copies** of messages to each other.

Real operating systems use both. We use *message passing* for communication (the events themselves) and a *lock* to protect the one shared structure inside the bus (the subscriber table).

### 3.3 What is a queue?

A FreeRTOS **queue** is a thread-safe waiting line (FIFO, first in first out). Key facts:

- It stores a fixed number of items of a fixed size, decided at creation.
- Sending **copies** the item into the queue. The sender's variable can be changed or destroyed right after.
- A receiver can **sleep** until an item arrives (`portMAX_DELAY` means "wait forever"), using no CPU.
- A sender can choose how long to wait if the queue is full. We choose **zero** (never wait).

### 3.4 What is a mutex?

A **mutex** (mutual exclusion lock) is like the key to a room with only one key.

```
take key  ->  do your work inside  ->  give key back
```

Anyone else who wants in has to wait until the key is returned. The rule that matters most: **always give the key back**, on every possible path out of the room. If you forget, everyone waits forever. That is a **deadlock**.

FreeRTOS mutexes also have **priority inheritance**. If a low-priority task holds the key and a high-priority task is waiting for it, the low-priority task is temporarily boosted so it can finish quickly. Without this, a medium-priority task could keep the low-priority one from running, and the high-priority task would be stuck behind it. That situation is called *priority inversion*.

---

## 4. Design decisions and why we made them

| Decision | The alternative | Why we chose this |
|---|---|---|
| Events are **small, fixed-size structs copied by value** | Send pointers to event objects | A pointer to a local variable is dead by the time the receiver runs, because the sender's stack frame is gone. Copies are always safe. |
| `publish()` **never blocks** | Wait for a free slot in a full mailbox | A slow listener must never freeze the module that is publishing. A frozen SD driver could miss real hardware events. |
| A **full mailbox drops** the event and counts it | Block, or grow the queue dynamically | Dropping is predictable and visible (`droppedCount()`). Growing memory at runtime is how embedded systems die after three weeks of uptime. |
| **Each listener has its own mailbox** | One global queue everyone reads | The UI task can handle UI events in the UI task. Slow listeners cannot hold up fast ones. |
| **Fixed subscriber table** (`config::maxSubscriptions`) | A linked list using `malloc` | No dynamic allocation at runtime means no fragmentation and a memory cost we can calculate in advance. |
| `enum class` for event types | Plain `enum` or `#define` numbers | Names cannot clash, and an event type cannot be silently mixed up with an ordinary integer. |
| `static_assert(sizeof(event) <= 16)` | A comment saying "keep it small" | A comment can be ignored. A compile error cannot. |
| **One exit point** per function, lock released once at the bottom | `return` from the middle of locked code | The most common RTOS bug is returning early while still holding the lock. One exit makes it nearly impossible. |
| Table hidden in an **anonymous namespace** | Make it a global in the header | No other file can touch or corrupt it. The only way in is through the API. |

### Why not just use ESP-IDF's built-in `esp_event`?

ESP-IDF already ships an event library, and in a production product you might use it. We wrote our own because:

1. **Learning.** Building a small version teaches the ideas behind every event system. After this you can read `esp_event` and understand it.
2. **Control.** We decide exactly what is copied, what can block and what gets dropped.
3. **Size.** Ours is about 150 lines you can read in one sitting.

We will still use `esp_event` underneath when we reach Wi-Fi, because the Wi-Fi stack publishes its events through it. Our Wi-Fi service will translate those into ORA-OS events.

---

## 5. Walking through the code

### 5.1 `events.h`: the vocabulary of the system

```cpp
enum class eventType : uint8_t {
    none = 0,
    bootComplete,       // arg0: unused, arg1: unused
    sdCardInserted,     // arg0: storage slot/port, arg1: unused
    sdCardRemoved,      // arg0: storage slot/port, arg1: unused
    wifiStateChanged,   // arg0: 0 = disconnected, 1 = connected, arg1: error/status
    volumeChanged,      // arg0: new volume (0-100), arg1: stream type
    count               // always last
};
```

- `: uint8_t` makes each type one byte, which keeps the event struct small.
- `none = 0` means an uninitialised event (all zeros) is recognisably "nothing" and not accidentally `bootComplete`.
- `count` is a trick: because it is always the last entry, its numeric value equals the number of real types. We use it to validate input.
- The **comments are part of the design**. `arg0` and `arg1` mean different things for different events, and the only documentation of that is these comments. When you add an event, document its arguments here.

```cpp
struct event {
    eventType type;
    uint32_t  arg0;
    uint32_t  arg1;
};
static_assert(sizeof(event) <= 16, "event struct exceeds 16 bytes");
```

**Why two generic arguments instead of a custom struct per event?** A queue needs *one* fixed item size. Two 32-bit numbers cover almost everything (a volume level, a slot number, an error code). The size on this chip is **12 bytes**: 1 byte for the type, 3 bytes of padding, and 4 bytes for each argument.

**What if I need to send a lot of data, like a whole file name?** Do not put it in the event. Send an event that says *"a file changed"* with an index or ID in `arg0`, and let the listener ask the storage service for details. Events are **notifications**, not data pipes.

### 5.2 `eventBus.h`: the public face

```cpp
bool          init();
QueueHandle_t createMailbox(uint8_t depth);
bool          subscribe(QueueHandle_t mailbox, events::eventType type);
bool          unsubscribe(QueueHandle_t mailbox, events::eventType type);
uint8_t       publish(const events::event &e);
uint32_t      droppedCount();
```

Six functions are the entire interface. Everything else is hidden in the `.cpp`.

`createMailbox` exists even though it is only one line (`xQueueCreate(depth, sizeof(events::event))`). The reason is that `xQueueCreate` needs the **item size**, and a wrong size silently corrupts memory. Putting that line in exactly one place means nobody can get it wrong.

### 5.3 `eventBus.cpp`: the machinery

**The subscriber table**

```cpp
struct subscription {
    QueueHandle_t     mailbox;
    events::eventType type;
    bool              used;
};
subscription table[config::maxSubscriptions];
```

Each row says: *"this mailbox wants this type of event"*. A slot with `used == false` is free. The table is a plain array of 32 rows (about 256 bytes of RAM) and never grows. A single mailbox may have several rows, one per event type it subscribes to.

**`init()`**

- Creates the mutex. If it returns `nullptr`, the system is out of memory, so we log and fail.
- Marks all slots as unused.
- Is safe to call twice: if the lock already exists it just returns `true`. Otherwise a second call would create a second mutex and leak the first.

**`subscribe()`**: step by step

```
1. refuse if init() was never called       (otherwise we'd crash on a null lock)
2. refuse null mailbox or invalid type     (catches typos early)
3. take the lock
4. scan the table once:
       - if this exact (mailbox, type) pair exists  -> already subscribed
       - remember the first free slot we pass
5. if already subscribed -> succeed without adding a row
   else if a free slot exists -> fill it
   else -> log "table full"
6. give the lock back   <- the ONE place it is released
```

Step 5 matters. Without the duplicate check, subscribing twice would create two rows. `publish()` would then deliver every event **twice** to the same listener, a bug that is confusing to track down.

**`publish()`**: the heart of the bus

```cpp
for (auto &sub : table) {
    if (sub.used && sub.type == e.type) {
        if (xQueueSend(sub.mailbox, &e, 0) == pdTRUE) delivered++;
        else                                          dropped++;
    }
}
```

- It walks the table and sends a **copy** of the event to every mailbox subscribed to this type.
- The `0` in `xQueueSend(..., 0)` is the timeout. Zero means "if the queue is full, give up immediately". This single character is what makes `publish` non-blocking.
- Because every send has zero timeout, the lock is only held for a very short, bounded time.
- It returns how many listeners received the event. Zero is valid, and means nobody was interested.

**`droppedCount()`**

It reads `dropped` without taking the lock. That is deliberate: a 32-bit aligned read is atomic on the ESP32, and this number is only used for diagnostics.

---

## 6. The life of one event

Here is what happens when the SD driver announces a card.

```mermaid
sequenceDiagram
    participant UI as UI task
    participant Bus as eventBus
    participant Box as UI mailbox (queue)
    participant SD as SD driver task

    UI->>Bus: subscribe(mailbox, sdCardInserted)
    UI->>Box: xQueueReceive (goes to sleep)
    Note over UI: blocked, using no CPU
    SD->>Bus: publish({sdCardInserted, slot 1, 0})
    Bus->>Bus: take lock, scan table
    Bus->>Box: xQueueSend(copy of event, timeout 0)
    Bus->>Bus: give lock
    Bus-->>SD: returns 1 (one listener reached)
    Box-->>UI: wakes up with its own copy
    UI->>UI: handle event (refresh the icon)
```

The driver never learned who was listening. The UI never learned who published. Adding a music player later means one more `subscribe()` call and no change to the driver at all.

---

## 7. How to use it

### Add a new event type

1. Add a name to `eventType` in `events.h`, **before** `count`.
2. Write a comment saying what `arg0` and `arg1` mean.
3. That is all. Nothing else in the bus needs to change.

### Become a listener

```cpp
static void myTask(void *param)
{
    QueueHandle_t mailbox = static_cast<QueueHandle_t>(param);
    events::event e;

    for (;;) {
        // sleep here until something arrives: costs no CPU
        if (xQueueReceive(mailbox, &e, portMAX_DELAY) == pdTRUE) {
            switch (e.type) {
                case events::eventType::sdCardInserted: /* react */ break;
                case events::eventType::sdCardRemoved:  /* react */ break;
                default: break;
            }
        }
    }
}

// somewhere during boot:
QueueHandle_t mailbox = eventBus::createMailbox(config::defaultMailboxDepth);
eventBus::subscribe(mailbox, events::eventType::sdCardInserted);
eventBus::subscribe(mailbox, events::eventType::sdCardRemoved);
xTaskCreate(myTask, "mine", 4096, mailbox, 5, nullptr);
```

### Announce something

```cpp
eventBus::publish({events::eventType::sdCardInserted, /*slot*/ 1, 0});
```

---

## 8. Things that will bite you

| Pitfall | Why it happens | What to do |
|---|---|---|
| **Calling `publish()` from an interrupt handler (ISR)** | A mutex cannot be taken inside an ISR, so this will crash or hang. | Use a task to forward the event, or (later) a dedicated `publishFromIsr()` that uses `xQueueSendFromISR`. |
| **A listener that is too slow** | Its mailbox fills up and events are dropped. | Keep handlers short. Do heavy work in another task. Raise the mailbox depth if bursts are normal. Watch `droppedCount()`. |
| **Putting a pointer to a local variable in `arg0`** | By the time the receiver runs, that stack memory has been reused. | Only put plain numbers in events. |
| **Forgetting `init()`** | Without it the lock is null. | Call `eventBus::init()` first thing in `app_main`. The other functions refuse politely and log an error. |
| **Never unsubscribing before deleting a task or queue** | The table still holds the mailbox handle, so `publish` would send to a dead queue. | Always `unsubscribe` before deleting a mailbox. |
| **Assuming ordering across different event types** | Each mailbox is FIFO, but two events published from different tasks have no guaranteed order. | Do not rely on cross-task ordering. Put what the listener needs in the event itself. |

---

## 9. Memory cost

| Item | Size |
|---|---|
| One event | 12 bytes |
| Subscriber table (32 slots) | about 256 bytes |
| One mailbox of depth 8 | 96 bytes of item storage plus about 100 bytes of queue bookkeeping |

The whole event system costs less than 1 KB of internal RAM for a handful of listeners.

---

## 10. Limits and what could come next

- **No wildcard subscriptions.** You subscribe to each type separately.
- **No priorities.** Every event is equal. A future "urgent" lane could use a second queue.
- **No ISR-safe publish yet.** This is needed for button or touch interrupts.
- **Fixed table size.** 32 subscriptions is plenty for now. Raising it is a one-line change in `config.h`.
- **No delivery guarantee.** A full mailbox drops events. That is acceptable for notifications ("the card changed"). If something must never be lost, such as a "save settings" command, it should use a different mechanism with acknowledgement.

---

## 11. Exercises

1. **Duplicate test.** Remove the duplicate check in `subscribe()`, subscribe twice, and watch every event arrive twice. Put the check back.
2. **Overflow test.** Make a listener sleep 5 seconds after each event while you publish 20 quickly. Predict `droppedCount()` first, then check.
3. **Add an event.** Create `batteryLow` with the percentage in `arg0`, and a second listener task that subscribes to it.
4. **Two listeners, one event.** Have two separate tasks subscribe to `sdCardInserted`. What does `publish()` return?
5. **Break it on purpose.** Return early from `subscribe()` without calling `xSemaphoreGive`. What happens on the next call, and what does that teach you about the single-exit style?

---

## 12. Glossary

| Term | Meaning |
|---|---|
| **Task** | A function with its own stack, scheduled by FreeRTOS. |
| **Scheduler** | The part of FreeRTOS that decides which task runs next. |
| **Blocked** | Asleep, waiting for something, using no CPU. |
| **Queue** | A thread-safe FIFO that copies items in and out. |
| **Mailbox** | In this project, a queue owned by one listener. |
| **Mutex** | A lock that only one task can hold at a time. |
| **Race condition** | A bug caused by two tasks touching shared data in an unlucky order. |
| **Deadlock** | Tasks waiting on each other (or on a lost lock) forever. |
| **Priority inversion** | A high-priority task stuck behind a low-priority one holding a lock. |
| **ISR** | Interrupt service routine: code that runs in response to a hardware interrupt. |
| **Publish/subscribe** | A pattern where senders announce events and receivers register interest, without knowing each other. |
| **Coupling** | How much one module needs to know about another. Less is better. |