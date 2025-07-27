#include <Arduino.h>
#include "screen/index.hpp"
#include "apps/windows.hpp"
#include "apps/index.hpp"

using namespace Windows;

void setup()
{
    Serial.begin(115200);
    delay(1000);

    // Initialize the display & touch
    Screen::init();
    // // Create + initialize a Window on the heap
    WindowPtr win(new Window());
    win->init("Hello World Test", Vec{10, 10});
    // // Add it into our window manager
    add(std::move(win));

    LuaApps::initialize(); // Initialisiere Serial + SPIFFS

    Serial.println("Running Lua app...");
    // Führt /test.lua im Sandbox-Modus aus
    int result = LuaApps::runApp("/test.lua", {"Arg1", "Hi"});
    Serial.printf("Lua App exited with code: %d\n", result);

    // win->sprite.print(result);
}

void loop()
{
    Windows::loop();
}
