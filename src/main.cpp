#include <Arduino.h>

#include "apps/window.hpp"

Window win;

void setup()
{
    Serial.begin(115200);
    Screen::init();
    win.init("Hello World Test");
}

void loop()
{
    win.loop();
}
