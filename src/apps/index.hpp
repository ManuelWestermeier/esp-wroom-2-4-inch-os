#pragma once

#include <vector>
#include <Arduino.h>
#include "functions.hpp"
#include "sandbox.hpp"
#include "network.hpp"
#include "runtime.hpp"
#include "app.hpp"
#include "system.hpp"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "../wifi/index.hpp"

// --- Globals ---
TaskHandle_t WindowAppRenderHandle = NULL;
TaskHandle_t WindowAppRunHandle = NULL;

std::vector<TaskHandle_t> runningTasks;

// ---------------------- App Run Task ----------------------
void AppRunTask(void *pvParameters)
{
    // pvParameters erwartet einen Zeiger auf eine heap-allokierte std::vector<String>
    auto taskArgsPtr = static_cast<std::vector<String> *>(pvParameters);
    // Kopiere die Argumente lokal und gib den heap-Speicher frei
    std::vector<String> args = *taskArgsPtr;
    delete taskArgsPtr;

    Serial.println("Running Lua app...");

    // Build app argument vector (args[0] = path; rest = args for app)
    std::vector<String> appArgs;
    if (args.size() > 1)
    {
        appArgs.assign(args.begin() + 1, args.end());
    }

    int result = LuaApps::runApp(args[0], appArgs);
    Serial.printf("Lua App exited with code: %d\n", result);

    // Task beenden
    vTaskDelete(NULL);
}

// --- Start an application in a new FreeRTOS task ---
// NOTE: args wird kopiert und die Kopie auf dem Heap übergeben (sicher für Task)
void executeApplication(const std::vector<String> &args)
{
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

    // Task erfolgreich erstellt -> handle speichern
    runningTasks.push_back(WindowAppRunHandle);
}

void AppRenderTask(void *pvParameters)
{
    (void)pvParameters;
    while (true)
    {
        Windows::loop();
        // vTaskDelay ist FreeRTOS-freundlich (besser als delay in einer Task)
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void startWindowRender()
{
    xTaskCreate(
        AppRenderTask,
        "AppRenderTask",
        2048,
        NULL,
        2,
        &WindowAppRenderHandle);
}

void debugTaskLog()
{
    Serial.println(String("MAX HEAP:") + ESP.getMaxAllocHeap());

    if (WindowAppRunHandle)
    {
        Serial.printf("AppRunTask free stack: %u bytes\n",
                      (unsigned int)(uxTaskGetStackHighWaterMark(WindowAppRunHandle) * sizeof(StackType_t)));
    }
    else
    {
        Serial.println("AppRunTask handle not set");
    }

    if (WindowAppRenderHandle)
    {
        Serial.printf("AppRenderTask free stack: %u bytes\n",
                      (unsigned int)(uxTaskGetStackHighWaterMark(WindowAppRenderHandle) * sizeof(StackType_t)));
    }
    else
    {
        Serial.println("AppRenderTask handle not set");
    }

    // UserWiFi::WiFiConnectTaskHandle muss an anderer Stelle definiert sein
    Serial.printf("WiFiConnectTask free stack: %u bytes\n",
                  (unsigned int)(uxTaskGetStackHighWaterMark(UserWiFi::WiFiConnectTaskHandle) * sizeof(StackType_t)));
}
