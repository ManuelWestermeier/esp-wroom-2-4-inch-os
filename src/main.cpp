#include <Arduino.h>
#include "screen/index.hpp"
#include "apps/windows.hpp"
#include "apps/index.hpp"

using namespace Windows;

WindowPtr win(new Window());

void setup()
{
    Serial.begin(115200);
    Serial.println("Booting MW 2.4i OS...");
    delay(1000);

    // Initialize the display & touch
    Screen::init();

    LuaApps::initialize(); // Initialisiere Serial + SPIFFS
    Serial.println("Running Lua app...");
    // Führt /test.lua im Sandbox-Modus aus in einen neuen prozess aus
    int result = LuaApps::runApp("/test.lua", {"Arg1", "Hi"});
    Serial.printf("Lua App exited with code: %d\n", result);
}

void loop()
{
    Windows::loop();
}
