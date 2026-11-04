#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "config.h"
#include "core/eventBus.h"
#include "core/events.h"
#include "core/taskManager.h"
#include "system/sysInfo.h"

static const char *tag = "main";

// listener: sleeps on its mailbox and logs every event it receives
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

// heartbeat: wakes at exact 1 s intervals (vTaskDelayUntil does not drift)
static void heartbeatTask(void *param)
{
    (void)param;
    TickType_t lastWake = xTaskGetTickCount();   // must be set once before the loop
    uint32_t   beats    = 0;

    for (;;) {
        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(1000));
        beats++;

        if (beats % 5 == 0) {
            eventBus::publish({events::eventType::sdCardInserted, 1, 0});
        }
        if (beats % 10 == 0) {
            taskManager::report();
        }
    }
}

// worker: simulates a task that computes for a moment, then sleeps
static void workerTask(void *param)
{
    (void)param;
    volatile uint32_t sink = 0;   // volatile stops the compiler deleting the "work"

    for (;;) {
        for (uint32_t i = 0; i < 200000; i++) {
            sink = sink + i;
        }
        vTaskDelay(pdMS_TO_TICKS(500));   // blocks, so lower-priority tasks and idle can run
    }
}

extern "C" void app_main(void)
{
    sysInfo::printBanner();

    // services first, in dependency order
    if (!eventBus::init() || !taskManager::init()) {
        ESP_LOGE(tag, "core service init failed");
        return;
    }

    QueueHandle_t mailbox = eventBus::createMailbox(config::defaultMailboxDepth);
    if (mailbox == nullptr) {
        ESP_LOGE(tag, "mailbox creation failed");
        return;
    }
    eventBus::subscribe(mailbox, events::eventType::bootComplete);
    eventBus::subscribe(mailbox, events::eventType::sdCardInserted);

    bool ok = true;

    ok = ok && taskManager::spawn(taskManager::taskSpec{
        .name       = "listener",
        .entryPoint = listenerTask,
        .stackBytes = config::stack::normal,
        .param      = mailbox,
        .priority   = config::prio::service,
        .core       = taskManager::coreId::core0
    });

    ok = ok && taskManager::spawn(taskManager::taskSpec{
        .name       = "heartbeat",
        .entryPoint = heartbeatTask,
        .stackBytes = config::stack::normal,
        .priority   = config::prio::background
    });

    ok = ok && taskManager::spawn(taskManager::taskSpec{
        .name       = "worker",
        .entryPoint = workerTask,
        .stackBytes = config::stack::normal,
        .priority   = config::prio::service,
        .core       = taskManager::coreId::core1
    });

    if (!ok) {
        ESP_LOGE(tag, "failed to start system tasks");
        return;
    }

    eventBus::publish({events::eventType::bootComplete, 0, 0});
    ESP_LOGI(tag, "boot complete");
}