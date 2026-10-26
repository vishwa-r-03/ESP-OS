#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "config.h"
#include "core/eventBus.h"
#include "core/events.h"
#include "system/sysinfo.h"

static const char *tag = "main";

// listener task: sleeps on its mailbox and logs every event it receives
static void listenerTask(void *param)
{
    QueueHandle_t mailbox = static_cast<QueueHandle_t>(param);
    events::event e;

    for (;;) {
        if (xQueueReceive(mailbox, &e, portMAX_DELAY) == pdTRUE) {
            ESP_LOGI(tag, "received event type %u (arg0=%u)",
                     static_cast<unsigned>(e.type), (unsigned)e.arg0);
        }
    }
}

extern "C" void app_main(void)
{
    sysInfo::printBanner();

    // 1. event bus
    if (!eventBus::init()) {
        ESP_LOGE(tag, "event bus init failed");
        return;
    }

    // 2. mailbox for the listener
    QueueHandle_t mailbox = eventBus::createMailbox(config::defaultMailboxDepth);
    if (mailbox == nullptr) {
        ESP_LOGE(tag, "mailbox creation failed");
        return;
    }

    // subscribe to bootComplete and sdCardInserted, deliberately NOT sdCardRemoved
    eventBus::subscribe(mailbox, events::eventType::bootComplete);
    eventBus::subscribe(mailbox, events::eventType::sdCardInserted);

    // 3. listener task (stack size is in BYTES on ESP-IDF)
    if (xTaskCreate(listenerTask, "listener", 4096, mailbox, 5, nullptr) != pdPASS) {
        ESP_LOGE(tag, "listener task creation failed");
        return;
    }

    // 4. publish a boot event, then simulate an SD card coming and going
    eventBus::publish({events::eventType::bootComplete, 0, 0});

    const TickType_t oneSecond = pdMS_TO_TICKS(1000);

    for (;;) {
        eventBus::publish({events::eventType::sdCardInserted, 1, 0});
        vTaskDelay(oneSecond);

        eventBus::publish({events::eventType::sdCardRemoved, 1, 0});
        vTaskDelay(oneSecond);
    }
}