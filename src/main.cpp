#include <Arduino.h>

// #include "./screen.hpp"
#include "./lua.hpp"

void setup()
{
    Serial.begin(115200);
    Serial.println("Booting...");

    if (!SPIFFS.begin(true))
    {
        Serial.println("SPIFFS init failed");
        return;
    }

    Serial.println("Running Lua...");
    LuaSandbox::init();
    LuaSandbox::runFile("/test.lua");
    LuaSandbox::cleanup();

    // Serial.println("Running Screen...");
    // screenInitTest();
}

void loop()
{
    // loopScreenTest();
}