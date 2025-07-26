#include <Arduino.h>
#include "screen/index.hpp"
#include "apps/windows.hpp"

using namespace Windows;

void setup()
{
    Serial.begin(115200);
    delay(1000);

    // Initialize the display & touch
    Screen::init();

    // Create + initialize a Window on the heap
    WindowPtr win(new Window());
    win->init("Hello World Test", Vec{10, 10});

    // Add it into our window manager
    add(std::move(win));

    // Create + initialize a Window on the heap
    WindowPtr win2(new Window());
    win2->init("Helllo 2", Vec{130, 130});

    // Add it into our window manager
    add(std::move(win2));
}

void loop()
{
    Windows::loop();
}
