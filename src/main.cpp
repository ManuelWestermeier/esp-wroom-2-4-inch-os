// #include "./apps/index.hpp"

// void setup()
// {
//     Serial.begin(115200);
//     Serial.println("Booting...");

//     LuaApps::initialize(); // Initialisiere Serial + SPIFFS
//     Serial.println("Running Lua app...");
//     // Führt /test.lua im Sandbox-Modus aus
//     int result = LuaApps::runApp("/test.lua", {"Arg1", "Hi"});
//     Serial.printf("Lua App exited with code: %d\n", result);
// }

#include <Arduino.h>
#include "screen/index.hpp"

void setup()
{
    Serial.begin(115200);
    Screen::init();
}

Vec off = {};

void loop()
{
    auto pos = Screen::getTouchPos();
    
    if (pos.clicked)
    {
        off.x += pos.move.x;
        off.y += pos.move.y;
    }
    
    off.print();
    
    Screen::tft.fillScreen(TFT_WHITE);
    Screen::tft.setCursor(off.x, off.y);
    Screen::tft.println("HELLO");
}