// #include <Arduino.h>

// #include <TFT_eSPI.h>
// #include <SD.h>

// #define SD_CS 5

// void test()
// {
//     static byte oldData[3] = {3, 3, 3}; // Invalid initial state to force first log
//     byte pins[3] = {SD_CS, TOUCH_CS, TFT_CS};
//     const char *names[3] = {"SD_CS", "TOUCH_CS", "TFT_CS"};

//     for (int i = 0; i < 3; ++i)
//     {
//         byte value = digitalRead(pins[i]);
//         if (value != oldData[i])
//         {
//             Serial.printf("[%lu ms] %s (%d): %s\n", millis(), names[i], pins[i], value ? "HIGH" : "LOW");
//             oldData[i] = value;
//         }
//     }
//     delay(300);
// }

// TFT_eSPI tft(320, 240);

// void setup()
// {
//     // display on
//     pinMode(27, OUTPUT);
//     digitalWrite(27, HIGH);

//     pinMode(SD_CS, OUTPUT);
//     digitalWrite(SD_CS, HIGH);
//     pinMode(TFT_CS, OUTPUT);
//     digitalWrite(TFT_CS, HIGH);
//     pinMode(TOUCH_CS, OUTPUT);
//     digitalWrite(TOUCH_CS, HIGH);

//     SD.init(SD_CS);

//     test();
//     Serial.begin(115200);
//     test();
//     tft.init();
//     test();
//     tft.fillScreen(0);
//     test();
//     tft.fillScreen(0xffff);
//     test();
//     tft.endWrite();
//     test();
//     tft.fillScreen(0x0000);
// }

// void loop()
// {
// }

#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "screen/index.hpp"
#include "apps/windows.hpp"
#include "apps/index.hpp"

#include "utils/time.hpp"

using namespace Windows;

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

const char *ssid = "LocalHost";
const char *password = "hhhhhhhy";

void setup()
{
    Serial.begin(115200);
    Serial.println("Booting MW 2.4i OS...\n");

    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }
    Serial.println("Verbunden!");

    UserTime::set();

    if (!Serial)
        delay(1000);

    // Initialize the display & touch
    Screen::init();
    LuaApps::initialize(); // Initialisiere SPIFFS

    Serial.println("Running Lua app task...");

    xTaskCreate(AppRunTask, "AppRunTask", 50000, NULL, 1, &WindowAppRunHandle);
    delay(300);
    xTaskCreate(AppRenderTask, "AppRenderTask", 2048, NULL, 2, &WindowAppRenderHandle);
}

void loop()
{
    Serial.println(ESP.getMaxAllocHeap());
    Serial.printf("AppRunTask stack high water mark: %d\n", uxTaskGetStackHighWaterMark(WindowAppRunHandle));
    Serial.printf("AppRenderTask stack high water mark: %d\n", uxTaskGetStackHighWaterMark(WindowAppRenderHandle));

    delay(1000);
}