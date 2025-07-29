#pragma once

#include <Arduino.h>
#include <vector>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_task_wdt.h"

#include "index.hpp"
#include "windows.hpp"

using namespace Windows;

namespace Apps
{
    // Task to run Lua app, receives pointer to dynamically allocated vector<String>
    void AppRunTask(void *rawArgs)
    {
        // Take ownership of args and delete after use
        auto args = *((std::vector<String> *)rawArgs);
        delete (std::vector<String> *)rawArgs;

        // Disable watchdog for this task to prevent resets
        esp_task_wdt_delete(NULL);

        Serial.println("Running Lua app...");

        // Example: run /test.lua with some arguments
        int result = LuaApps::runApp("/test.lua", {"Arg1", "Hi"});
        Serial.printf("Lua App exited with code: %d\n", result);

        vTaskDelete(NULL); // Cleanly kill this task
    }

    void startApp(std::vector<String> args = {"/test.lua", "arg1", "arg2"})
    {
        if (args.empty())
        {
            Serial.println("<ERROR RUNNING APP: ARGS.LEN == 0>");
            return;
        }

        // Allocate on heap because tasks run asynchronously
        std::vector<String> *argsCopy = new std::vector<String>(args);

        // Build task name safely (must persist until task creation)
        String taskName = "AppRunTask#" + args[0];

        TaskHandle_t WindowAppRunHandle = NULL;

        // Create the task, passing pointer to argsCopy
        BaseType_t res = xTaskCreate(
            AppRunTask,
            taskName.c_str(),
            8192, // Stack size (adjust as needed)
            (void *)argsCopy,
            1, // Priority
            &WindowAppRunHandle);

        if (res != pdPASS)
        {
            Serial.println("Failed to create AppRunTask");
            delete argsCopy; // prevent memory leak on failure
        }
    }

    TaskHandle_t WindowsAppRenderTaskHandle = NULL;

    void AppRenderTask(void *)
    {
        // Disable watchdog for this render task
        esp_task_wdt_delete(NULL);

        while (true)
        {
            Windows::loop();
            delay(10); // Yield CPU, prevent watchdog resets
        }
    }

    void startRender()
    {
        xTaskCreate(
            AppRenderTask,
            "AppRenderTask",
            4096, // Stack size (adjust as needed)
            NULL,
            2, // Priority higher than app run
            &WindowsAppRenderTaskHandle);
    }

    void debugLoop()
    {
        Serial.println(ESP.getMaxAllocHeap());
        Serial.printf("AppRenderTask stack high water mark: %d\n",
                      uxTaskGetStackHighWaterMark(WindowsAppRenderTaskHandle));
        delay(5000);
    }

    void init()
    {
        // Initialize display & touch
        Screen::init();

        // Initialize Lua environment, SPIFFS, etc.
        LuaApps::initialize();

        // Start the rendering task
        startRender();
    }
}
