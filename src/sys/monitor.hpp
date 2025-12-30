#pragma once

#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "../apps/index.hpp"

// ---------------------- Debug (single-shot) ----------------------
void debugTaskLog()
{
    Serial.println("Min,Nor,Max");
    Serial.println(ESP.getMinFreeHeap());
    Serial.println(ESP.getFreeHeap());
    Serial.println(ESP.getMaxAllocHeap());

    if (WindowAppRenderHandle)
    {
        Serial.printf("AppRenderTask free stack: %u bytes\n",
                      (unsigned int)(uxTaskGetStackHighWaterMark(WindowAppRenderHandle) * sizeof(StackType_t)));
    }
    else
    {
        Serial.println("AppRenderTask handle not set");
    }

    // Optional: Alle bekannten Tasks einmalig loggen
    std::vector<TaskHandle_t> snapshot;
    RUNNING_TASKS_LOCK();
    snapshot = runningTasks;
    RUNNING_TASKS_UNLOCK();

    for (TaskHandle_t h : snapshot)
    {
        if (!h)
            continue;
        UBaseType_t highWords = uxTaskGetStackHighWaterMark(h);
        unsigned int highBytes = (unsigned int)(highWords * sizeof(StackType_t));
        const char *name = pcTaskGetTaskName(h);
        if (!name)
            name = "?";
        Serial.printf("[debugTaskLog] Task %p name=%s highWater=%u bytes\n",
                      (void *)h, name, highBytes);
    }
}

void monitor()
{
    static uint32_t last = 0;
    if (millis() - last < 5000)
        return; // 5s interval
    last = millis();

    Serial.printf(
        "heap=%u  min=%u  largest=%u  tasks=%u\n",
        ESP.getFreeHeap(),
        ESP.getMinFreeHeap(),
        heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
        uxTaskGetNumberOfTasks());

    debugTaskLog();
}