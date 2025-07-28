#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "screen/index.hpp"
#include "apps/windows.hpp"
#include "apps/index.hpp"

using namespace Windows;

WindowPtr win(new Window());

TaskHandle_t WindowAppRunndle = NULL;

void AppRunTask(void *)
{
    // run app
    Serial.println("Running Lua app...");
    // Führt /test.lua im Sandbox-Modus aus in einen neuen prozess aus
    int result = LuaApps::runApp("/test.lua", {"Arg1", "Hi"});
    Serial.printf("Lua App exited with code: %d\n", result);
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
    xTaskCreate(AppRunTask, "AppRunTask", 1 << 12, NULL, 1, &WindowAppRunndle);
    Serial.println("Running loop...");
}

void loop()
{
    Windows::loop();
    delay(10);
}
