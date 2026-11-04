#pragma once
#include <cstdint>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace taskManager{

    enum class coreId : int{
        core0 = 0,
        core1 = 1,
        any   = tskNO_AFFINITY
    };

    // specification structure used to define and spawn managed tasks
    struct taskSpec{
        const char *name;             //Task name (for debugging / FreeRTOS tracking)
        TaskFunction_t entryPoint;    //Task entry point (function pointer)
        uint32_t stackBytes;          //Stack size in bytes (not words) for ESP-IDF
        void *param = nullptr;        //Parameter pointer passed to the task entry function
        UBaseType_t priority;         //Task priority (0=lowest, configMAX_PRIORITIES-1=highest)
        coreId core = coreId::any;    //Target CPU core (or taskNO_AFFINITY)
    };

    // call once at boot, before any spawn()
    bool init();

    // Spawns a new task using the provided specification
    bool spawn(const taskSpec &spec, TaskHandle_t *outHandle = nullptr);

    // Returns the total number of managed tasks currently running
    uint8_t count();

    // Logs one line per managed task for runtime diagnostics/reporting
    void report();

} // namespace taskManager