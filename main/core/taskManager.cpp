#include "taskManager.h"
#include "config.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/semphr.h"
#include "board/boardEsp32s3N16r8.h" 

static const char *tag = "taskManager";

namespace {

    struct record {
        const char   *name;
        TaskHandle_t  handle;
    };

    // Fixed-size table and counter: no dynamic allocations at runtime
    record              table[config::maxManagedTasks];
    uint8_t             managedCount = 0;

    // Mutex to protect the task record table against concurrent spawning/reporting
    SemaphoreHandle_t   tableLock = nullptr;

    bool ensureInit() {
        if (tableLock == nullptr) {
            tableLock = xSemaphoreCreateMutex();
            if (tableLock == nullptr) {
                ESP_LOGE(tag, "Failed to create task manager table lock");
                return false;
            }
        }
        return true;
    }

    const char* stateName(eTaskState s) {
        switch (s) {
            case eRunning:   return "Running";
            case eReady:     return "Ready";
            case eBlocked:   return "Blocked";
            case eSuspended: return "Suspended";
            case eDeleted:   return "Deleted";
            default:         return "?";
        }
    }

} // namespace

namespace taskManager {

/*
 * NOTE ON WHO CALLS spawn():
 * spawn() can be called safely from any task or during boot initialization 
 * because access to the internal task record table is protected by a 
 * FreeRTOS mutex (`tableLock`), exactly following the thread-safety pattern 
 * established in the event bus.
 */
bool spawn(const taskSpec &spec, TaskHandle_t *outHandle)
{
    if (!ensureInit()) {
        return false;
    }

    if (spec.entry == nullptr || spec.name == nullptr) {
        ESP_LOGE(tag, "Cannot spawn task with null entry or name");
        return false;
    }

    if (xSemaphoreTake(tableLock, portMAX_DELAY) != pdTRUE) {
        return false;
    }

    if (managedCount >= config::maxManagedTasks) {
        ESP_LOGE(tag, "Task table full (max: %u)", (unsigned)config::maxManagedTasks);
        xSemaphoreGive(tableLock);
        return false;
    }

    // If the board has fewer than 2 cores, force core affinity to "any" 
    // to prevent pinning failures on single-core configurations.
    taskManager::coreId effectiveCore = spec.core;
    if (board::cpuCores < 2) {
        effectiveCore = taskManager::coreId::any;
    }

    TaskHandle_t handle = nullptr;
    BaseType_t result = xTaskCreatePinnedToCore(
        spec.entry,
        spec.name,
        spec.stackBytes,
        spec.param,
        spec.priority,
        &handle,
        static_cast<BaseType_t>(effectiveCore)
    );

    if (result != pdPASS || handle == nullptr) {
        ESP_LOGE(tag, "Failed to create task: %s", spec.name);
        xSemaphoreGive(tableLock);
        return false;
    }

    // Store in our tracking table
    table[managedCount].name = spec.name;
    table[managedCount].handle = handle;
    managedCount++;

    if (outHandle != nullptr) {
        *outHandle = handle;
    }

    ESP_LOGI(tag, "Spawned task '%s' (prio=%u, core=%d)", 
             spec.name, (unsigned)spec.priority, static_cast<int>(effectiveCore));

    xSemaphoreGive(tableLock);
    return true;
}

uint8_t count()
{
    if (!ensureInit()) {
        return 0;
    }
    if (xSemaphoreTake(tableLock, portMAX_DELAY) != pdTRUE) {
        return 0;
    }
    uint8_t cnt = managedCount;
    xSemaphoreGive(tableLock);
    return cnt;
}

void report()
{
    if (!ensureInit()) {
        return;
    }
    if (xSemaphoreTake(tableLock, portMAX_DELAY) != pdTRUE) {
        return;
    }

    ESP_LOGI(tag, "=== Task Manager Report ===");
    for (uint8_t i = 0; i < managedCount; i++) {
        const auto &rec = table[i];
        if (rec.handle == nullptr) continue;

        BaseType_t coreIdNum = xTaskGetCoreID(rec.handle);
        UBaseType_t prio = uxTaskPriorityGet(rec.handle);
        eTaskState state = eTaskGetState(rec.handle);
        // ESP-IDF uxTaskGetStackHighWaterMark returns bytes directly
        uint32_t highWaterMark = uxTaskGetStackHighWaterMark(rec.handle);

        ESP_LOGI(tag, "Task: %-12s | Core: %d | Prio: %u | State: %-9s | Stack Free: %lu bytes",
                 rec.name, 
                 (int)coreIdNum, 
                 (unsigned)prio, 
                 stateName(state), 
                 (unsigned long)highWaterMark);
    }

    // Memory status report alongside tasks
    size_t freeInternal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t freePsram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    
    ESP_LOGI(tag, "Memory Report -> Free Internal RAM: %u bytes | Free PSRAM: %u bytes", 
             (unsigned)freeInternal, (unsigned)freePsram);
    ESP_LOGI(tag, "===========================");

    xSemaphoreGive(tableLock);
}

} // namespace taskManager