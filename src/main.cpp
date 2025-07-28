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
    // // Create + initialize a Window on the heap
    win->init("Test App", Vec{30, 30});
    // // Add it into our window manager
    Window *rawWin = win.get(); // grab raw pointer before move
    add(std::move(win));        // transfer ownership

    LuaApps::initialize(); // Initialisiere Serial + SPIFFS
    Serial.println("Running Lua app...");
    // Führt /test.lua im Sandbox-Modus aus
    int result = LuaApps::runApp("/test.lua", {"Arg1", "Hi"});
    Serial.printf("Lua App exited with code: %d\n", result);

    rawWin->sprite.fillSprite(TFT_WHITE);
    rawWin->sprite.setTextColor(TFT_BLACK);
    rawWin->sprite.drawString(String("Result: ") + result, 10, 10, 2);
    rawWin->rightSprite.fillSprite(RGB(200, 240, 100));
}

void loop()
{
    Windows::loop();
}
