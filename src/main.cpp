// #include <Arduino.h>

// #include "apps/apps.hpp"

// void setup()
// {
//     Serial.begin(115200);
//     Serial.println("Booting MW 2.4i OS...");

//     if (!Serial)
//         delay(1000);

//     Apps::init();

//     Serial.println("Running Lua app task...");
//     Apps::startApp();
// }

// void loop()
// {
//    Apps::debugLoop();
// }

#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "screen/index.hpp"
#include "apps/windows.hpp"
#include "apps/index.hpp"

using namespace Windows;

WindowPtr win(new Window());

TaskHandle_t WindowAppRunHandle = NULL;

void AppRunTask(void *)
{
    // run app
    Serial.println("Running Lua app...");
    // Führt /test.lua im Sandbox-Modus aus in einen neuen prozess aus
    int result = LuaApps::runApp("/test.lua", {"Arg1", "Hi"});
    Serial.printf("Lua App exited with code: %d\n", result);
    vTaskDelete(NULL); // kill task cleanly
}

TaskHandle_t WindowAppRenderHandle = NULL;

void AppRenderTask(void *)
{
    while (true)
    {
        Windows::loop();
        delay(10);
    }
}

void setup()
{
    Serial.begin(115200);
    Serial.println("Booting MW 2.4i OS...");

    if (!Serial)
        delay(1000);

    // Initialize the display & touch
    Screen::init();
    LuaApps::initialize(); // Initialisiere SPIFFS

    Serial.println("Running Lua app task...");

    xTaskCreate(AppRunTask, "AppRunTask", 50000, NULL, 1, &WindowAppRunHandle);
    delay(100);
    xTaskCreate(AppRenderTask, "AppRenderTask", 2048, NULL, 2, &WindowAppRenderHandle);
}

void loop()
{
    Serial.println(ESP.getMaxAllocHeap());
    Serial.printf("AppRunTask stack high water mark: %d\n", uxTaskGetStackHighWaterMark(WindowAppRunHandle));
    Serial.printf("AppRenderTask stack high water mark: %d\n", uxTaskGetStackHighWaterMark(WindowAppRenderHandle));

    delay(1000);
}