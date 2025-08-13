#pragma once

#include <vector>
#include <algorithm>
#include <Arduino.h>
#include "functions.hpp"
#include "sandbox.hpp"
#include "network.hpp"
#include "runtime.hpp"
#include "app.hpp"
#include "system.hpp"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "../wifi/index.hpp"

// --- Globals ---
TaskHandle_t WindowAppRenderHandle = NULL;

static std::vector<TaskHandle_t> runningTasks;
static SemaphoreHandle_t runningTasksMutex = NULL;

// Ensure mutex exists (lazy init)
static void ensureRunningTasksMutex()
{
    if (runningTasksMutex == NULL)
    {
        runningTasksMutex = xSemaphoreCreateMutex();
        if (runningTasksMutex == NULL)
        {
            Serial.println("ERROR: failed to create runningTasksMutex");
        }
    }
}

// Add a task handle to runningTasks (no duplicates)
static void addRunningTask(TaskHandle_t h)
{
    if (h == NULL)
        return;
    ensureRunningTasksMutex();
    if (runningTasksMutex == NULL)
        return; // couldn't create

    if (xSemaphoreTake(runningTasksMutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        // only push if not present already
        if (std::find(runningTasks.begin(), runningTasks.end(), h) == runningTasks.end())
        {
            runningTasks.push_back(h);
        }
        xSemaphoreGive(runningTasksMutex);
    }
}

// Remove a task handle from runningTasks
static void removeRunningTask(TaskHandle_t h)
{
    if (h == NULL)
        return;
    ensureRunningTasksMutex();
    if (runningTasksMutex == NULL)
        return;

    if (xSemaphoreTake(runningTasksMutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        auto it = std::find(runningTasks.begin(), runningTasks.end(), h);
        if (it != runningTasks.end())
        {
            runningTasks.erase(it);
        }
        xSemaphoreGive(runningTasksMutex);
    }
}

// ---------------------- App Run Task ----------------------
void AppRunTask(void *pvParameters)
{
    // pvParameters erwartet einen Zeiger auf eine heap-allokierte std::vector<String>
    auto taskArgsPtr = static_cast<std::vector<String> *>(pvParameters);
    std::vector<String> args = *taskArgsPtr;
    delete taskArgsPtr;

    // Mark this task as running (in case caller didn't add it yet) - addRunningTask ist duplikat-sicher
    addRunningTask(xTaskGetCurrentTaskHandle());

    Serial.println("Running Lua app...");

    // Build app argument vector (args[0] = path; rest = args for app)
    std::vector<String> appArgs;
    if (args.size() > 1)
    {
        appArgs.assign(args.begin() + 1, args.end());
    }

    int result = LuaApps::runApp(args[0], appArgs);
    Serial.printf("Lua App exited with code: %d\n", result);

    // remove self from runningTasks (best-effort)
    removeRunningTask(xTaskGetCurrentTaskHandle());

    // Task beenden
    vTaskDelete(NULL);
}

// --- Start an application in a new FreeRTOS task ---
// NOTE: args wird kopiert und die Kopie auf dem Heap übergeben (sicher für Task)
void executeApplication(const std::vector<String> &args)
{
    TaskHandle_t WindowAppRunHandle = NULL;

    if (args.empty())
    {
        Serial.println("ERROR: no execute path specified");
        return;
    }

    // Erstelle eine heap-allokierte Kopie der Argumente, die der Task freigibt
    auto taskArgsPtr = new std::vector<String>(args);

    BaseType_t res = xTaskCreate(
        AppRunTask,         // task function
        "AppRunTask",       // name
        2048 * 4,           // stack (words)
        taskArgsPtr,        // parameter (heap-allocated copy)
        1,                  // priority
        &WindowAppRunHandle // task handle
    );

    if (res != pdPASS)
    {
        Serial.println("ERROR: failed to create AppRunTask");
        // bei Fehler Speicher freigeben
        delete taskArgsPtr;
        WindowAppRunHandle = NULL;
        return;
    }

    // Task erfolgreich erstellt -> handle speichern (duplikat-sicher)
    addRunningTask(WindowAppRunHandle);
}

// AppRenderTask
void AppRenderTask(void *pvParameters)
{
    (void)pvParameters;
    // mark self as running (in case not added)
    addRunningTask(xTaskGetCurrentTaskHandle());

    while (true)
    {
        Windows::loop();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void startWindowRender()
{
    BaseType_t res = xTaskCreate(
        AppRenderTask,
        "AppRenderTask",
        2048,
        NULL,
        2,
        &WindowAppRenderHandle);

    if (res != pdPASS)
    {
        Serial.println("ERROR: failed to create AppRenderTask");
        WindowAppRenderHandle = NULL;
        return;
    }

    addRunningTask(WindowAppRenderHandle);
}

// ---------------------- Task Monitor ----------------------
// Prints high water marks and removes deleted tasks from runningTasks
// ---------------------- Task Monitor ----------------------
// Prints high water marks, free heap, and removes deleted tasks
void TaskMonitor(void *pvParameters)
{
    (void)pvParameters;
    ensureRunningTasksMutex();

    while (true)
    {
        Serial.printf("[TaskMonitor] Free heap: %u bytes\n", ESP.getFreeHeap());

        if (runningTasksMutex != NULL &&
            xSemaphoreTake(runningTasksMutex, pdMS_TO_TICKS(200)) == pdTRUE)
        {
            for (size_t i = 0; i < runningTasks.size();)
            {
                TaskHandle_t h = runningTasks[i];
                eTaskState state = eTaskGetState(h);

                if (state == eDeleted)
                {
                    Serial.printf("Task %p state=DELETED -> removing from runningTasks\n", (void *)h);
                    runningTasks.erase(runningTasks.begin() + i);
                    continue;
                }
                else
                {
                    UBaseType_t highWords = uxTaskGetStackHighWaterMark(h);
                    unsigned int highBytes = (unsigned int)(highWords * sizeof(StackType_t));
                    Serial.printf("Task %p state=%d highWater=%u bytes\n",
                                  (void *)h, (int)state, highBytes);
                }
                ++i;
            }
            xSemaphoreGive(runningTasksMutex);
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void startTaskMonitor(unsigned priority = 1)
{
    // create mutex if needed
    ensureRunningTasksMutex();

    TaskHandle_t monitorHandle = NULL;
    BaseType_t res = xTaskCreate(
        TaskMonitor,
        "TaskMonitor",
        2048,
        NULL,
        priority,
        &monitorHandle);

    if (res != pdPASS)
    {
        Serial.println("ERROR: failed to create TaskMonitor");
        return;
    }

    addRunningTask(monitorHandle);
}

// ---------------------- Debug (single-shot) ----------------------
void debugTaskLog()
{
    Serial.println(String("MAX HEAP:") + ESP.getMaxAllocHeap());

    if (WindowAppRenderHandle)
    {
        Serial.printf("AppRenderTask free stack: %u bytes\n",
                      (unsigned int)(uxTaskGetStackHighWaterMark(WindowAppRenderHandle) * sizeof(StackType_t)));
    }
    else
    {
        Serial.println("AppRenderTask handle not set");
    }
}
