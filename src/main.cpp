#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "fs/index.hpp"
#include "screen/index.hpp"
#include "apps/windows.hpp"
#include "apps/index.hpp"
#include "wifi/index.hpp"

using namespace Windows;

TaskHandle_t WindowAppRunHandle = NULL;
TaskHandle_t WindowAppRenderHandle = NULL;

// ---------------------- App Run Task ----------------------
void AppRunTask(void *)
{
    Serial.println("Running Lua app...");
    int result = LuaApps::runApp("/test.lua", {"Arg1", "Hi"});
    Serial.printf("Lua App exited with code: %d\n", result);
    vTaskDelete(NULL);
}

// ---------------------- App Render Task ----------------------
void AppRenderTask(void *)
{
    while (true)
    {
        Windows::loop();
        delay(10);
    }
}

// ---------------------- Setup ----------------------
void setup()
{
    Serial.begin(115200);
    Serial.println("Booting MW 2.4i OS...\n");

    SD_FS::init();
    Screen::init();
    UserWiFi::start();
    LuaApps::initialize();

    xTaskCreate(AppRunTask, "AppRunTask", 2048 * 4, NULL, 1, &WindowAppRunHandle);
    delay(300);
    xTaskCreate(AppRenderTask, "AppRenderTask", 2048, NULL, 2, &WindowAppRenderHandle);
}

void loop()
{
    Serial.println(ESP.getMaxAllocHeap());
    Serial.printf("AppRunTask stack high water mark: %d\n", uxTaskGetStackHighWaterMark(WindowAppRunHandle));
    Serial.printf("AppRenderTask stack high water mark: %d\n", uxTaskGetStackHighWaterMark(WindowAppRenderHandle));
    Serial.printf("WiFiConnectTask stack high water mark: %d\n", uxTaskGetStackHighWaterMark(UserWiFi::WiFiConnectTaskHandle));

    delay(1000);
}
