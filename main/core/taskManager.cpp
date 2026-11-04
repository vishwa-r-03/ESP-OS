#include "taskManager.h"
#include "config.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/semphr.h"
#include <cstdio>

static const char *tag = "taskManager";

namespace {

    // anything smaller than this is certainly a mistake (ESP_LOGI alone needs about 3 KB)
    constexpr uint32_t minStackBytes = 1024;

    // fixed-size table of handles: no malloc at runtime.
    // Only handles are stored: the name is read back with pcTaskGetName(),
    // because FreeRTOS keeps its own copy and a caller's pointer could dangle.
    TaskHandle_t      table[config::maxManagedTasks];
    uint8_t           managedCount = 0;

    // protects table and managedCount against concurrent spawn()/report()
    SemaphoreHandle_t tableLock = nullptr;

    const char *stateName(eTaskState s)
    {
        switch (s) {
            case eRunning:   return "Running";
            case eReady:     return "Ready";
            case eBlocked:   return "Blocked";
            case eSuspended: return "Suspended";
            case eDeleted:   return "Deleted";
            default:         return "?";
        }
    }

    // unpinned tasks report tskNO_AFFINITY, which is not a number worth printing
    void coreText(BaseType_t core, char *out, size_t size)
    {
        if (core == tskNO_AFFINITY) {
            snprintf(out, size, "any");
        } else {
            snprintf(out, size, "%d", (int)core);
        }
    }

} // namespace

namespace taskManager {

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

    for (auto &handle : table) {
        handle = nullptr;
    }
    managedCount = 0;

    ESP_LOGI(tag, "ready (%u task slots)", (unsigned)config::maxManagedTasks);
    return true;
}

bool spawn(const taskSpec &spec, TaskHandle_t *outHandle)
{
    if (tableLock == nullptr) {
        ESP_LOGE(tag, "spawn called before init");
        return false;
    }
    if (spec.entryPoint == nullptr || spec.name == nullptr) {
        ESP_LOGE(tag, "spawn: null entry point or name");
        return false;
    }
    if (spec.priority >= (UBaseType_t)configMAX_PRIORITIES) {
        ESP_LOGE(tag, "spawn '%s': priority %u is out of range (max %u)",
                 spec.name, (unsigned)spec.priority, (unsigned)(configMAX_PRIORITIES - 1));
        return false;
    }
    if (spec.stackBytes < minStackBytes) {
        ESP_LOGE(tag, "spawn '%s': stack of %u bytes is too small",
                 spec.name, (unsigned)spec.stackBytes);
        return false;
    }

    // pinning to core 1 on a single-core chip would fail, so fall back to "any".
    // board::cpuCores is known at compile time, so this branch costs nothing.
    coreId core = spec.core;
    if constexpr (board::cpuCores < 2) {
        core = coreId::any;
    }

    if (xSemaphoreTake(tableLock, portMAX_DELAY) != pdTRUE) {
        return false;
    }

    bool ok = false;

    if (managedCount >= config::maxManagedTasks) {
        ESP_LOGE(tag, "task table full (max %u)", (unsigned)config::maxManagedTasks);
    } else {
        TaskHandle_t handle = nullptr;
        BaseType_t created = xTaskCreatePinnedToCore(
            spec.entryPoint,
            spec.name,
            spec.stackBytes,
            spec.param,
            spec.priority,
            &handle,
            static_cast<BaseType_t>(core));

        if (created != pdPASS || handle == nullptr) {
            ESP_LOGE(tag, "failed to create task '%s' (out of memory?)", spec.name);
        } else {
            table[managedCount] = handle;
            managedCount++;

            if (outHandle != nullptr) {
                *outHandle = handle;
            }

            char coreBuf[8];
            coreText(static_cast<BaseType_t>(core), coreBuf, sizeof(coreBuf));
            ESP_LOGI(tag, "spawned '%s' (prio=%u, core=%s, stack=%u)",
                     spec.name, (unsigned)spec.priority, coreBuf, (unsigned)spec.stackBytes);
            ok = true;
        }
    }

    xSemaphoreGive(tableLock);   // the only exit: lock is always released
    return ok;
}

uint8_t count()
{
    // lock-free read on purpose: a single aligned byte is read atomically
    return managedCount;
}

void report()
{
    if (tableLock == nullptr) {
        return;
    }

    // 1. copy the handles under the lock, then release it immediately.
    //    Printing is slow and must not block spawn().
    TaskHandle_t snapshot[config::maxManagedTasks];
    uint8_t n = 0;

    if (xSemaphoreTake(tableLock, portMAX_DELAY) != pdTRUE) {
        return;
    }
    n = managedCount;
    for (uint8_t i = 0; i < n; i++) {
        snapshot[i] = table[i];
    }
    xSemaphoreGive(tableLock);

    // 2. print from the copy
    ESP_LOGI(tag, "=== task report (%u tasks) ===", (unsigned)n);

    for (uint8_t i = 0; i < n; i++) {
        TaskHandle_t h = snapshot[i];

        char coreBuf[8];
        coreText(xTaskGetCoreID(h), coreBuf, sizeof(coreBuf));

        // on ESP-IDF the high-water mark is already in BYTES
        ESP_LOGI(tag, "%-12s | core %-3s | prio %2u | %-9s | stack free %5lu B",
                 pcTaskGetName(h),
                 coreBuf,
                 (unsigned)uxTaskPriorityGet(h),
                 stateName(eTaskGetState(h)),
                 (unsigned long)uxTaskGetStackHighWaterMark(h));
    }

    ESP_LOGI(tag, "free internal RAM: %u KB | free PSRAM: %u KB",
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
    ESP_LOGI(tag, "==============================");
}

} // namespace taskManager