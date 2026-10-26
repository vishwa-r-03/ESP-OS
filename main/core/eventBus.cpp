#include "eventBus.h"
#include "config.h"
#include "esp_log.h"
#include "freertos/semphr.h"

static const char *tag = "eventBus";

namespace {

    struct subscription {
        QueueHandle_t     mailbox;
        events::eventType type;
        bool              used;
    };

    // fixed-size table: no malloc at runtime, memory use is predictable
    subscription      table[config::maxSubscriptions];

    // protects the table (and `dropped`) against concurrent access from several tasks
    SemaphoreHandle_t tableLock = nullptr;

    // events lost because a subscriber's mailbox was full
    uint32_t          dropped   = 0;

    bool isValidType(events::eventType type)
    {
        return type != events::eventType::none &&
               static_cast<uint8_t>(type) < static_cast<uint8_t>(events::eventType::count);
    }

} // namespace

namespace eventBus {

bool init()
{
    if (tableLock != nullptr) {
        return true;    // already initialised
    }

    tableLock = xSemaphoreCreateMutex();
    if (tableLock == nullptr) {
        ESP_LOGE(tag, "failed to create table lock");
        return false;
    }

    for (auto &sub : table) {
        sub.mailbox = nullptr;
        sub.type    = events::eventType::none;
        sub.used    = false;
    }

    dropped = 0;
    ESP_LOGI(tag, "ready (%u subscription slots)", (unsigned)config::maxSubscriptions);
    return true;
}

QueueHandle_t createMailbox(uint8_t depth)
{
    return xQueueCreate(depth, sizeof(events::event));
}

bool subscribe(QueueHandle_t mailbox, events::eventType type)
{
    if (tableLock == nullptr) {
        ESP_LOGE(tag, "subscribe called before init");
        return false;
    }
    if (mailbox == nullptr || !isValidType(type)) {
        ESP_LOGE(tag, "subscribe: bad mailbox or event type");
        return false;
    }
    if (xSemaphoreTake(tableLock, portMAX_DELAY) != pdTRUE) {
        return false;
    }

    bool result      = false;
    bool alreadyThere = false;
    subscription *freeSlot = nullptr;

    for (auto &sub : table) {
        if (sub.used) {
            if (sub.mailbox == mailbox && sub.type == type) {
                alreadyThere = true;
                break;
            }
        } else if (freeSlot == nullptr) {
            freeSlot = &sub;
        }
    }

    if (alreadyThere) {
        result = true;
    } else if (freeSlot != nullptr) {
        freeSlot->mailbox = mailbox;
        freeSlot->type    = type;
        freeSlot->used    = true;
        result = true;
    } else {
        ESP_LOGE(tag, "subscription table full");
    }

    xSemaphoreGive(tableLock);   // the only exit: lock is always released
    return result;
}

bool unsubscribe(QueueHandle_t mailbox, events::eventType type)
{
    if (tableLock == nullptr) {
        ESP_LOGE(tag, "unsubscribe called before init");
        return false;
    }
    if (mailbox == nullptr || !isValidType(type)) {
        ESP_LOGE(tag, "unsubscribe: bad mailbox or event type");
        return false;
    }
    if (xSemaphoreTake(tableLock, portMAX_DELAY) != pdTRUE) {
        return false;
    }

    bool result = false;

    for (auto &sub : table) {
        if (sub.used && sub.mailbox == mailbox && sub.type == type) {
            sub.used = false;
            result = true;
            break;
        }
    }

    if (!result) {
        ESP_LOGW(tag, "unsubscribe: subscription not found");
    }

    xSemaphoreGive(tableLock);
    return result;
}

uint8_t publish(const events::event &e)
{
    if (tableLock == nullptr) {
        return 0;
    }
    if (xSemaphoreTake(tableLock, portMAX_DELAY) != pdTRUE) {
        return 0;
    }

    uint8_t delivered = 0;

    for (auto &sub : table) {
        if (sub.used && sub.type == e.type) {
            // timeout 0: never block. a full mailbox means we drop and count.
            if (xQueueSend(sub.mailbox, &e, 0) == pdTRUE) {
                delivered++;
            } else {
                dropped++;
            }
        }
    }

    xSemaphoreGive(tableLock);
    return delivered;
}

uint32_t droppedCount()
{
    // lock-free read on purpose: a 32-bit aligned read is atomic on the ESP32
    // and this value is only used for diagnostics
    return dropped;
}

} // namespace eventBus