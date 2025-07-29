#include <Arduino.h>

#include "apps/apps.hpp"

void setup()
{
    Serial.begin(115200);
    Serial.println("Booting MW 2.4i OS...");

    if (!Serial)
        delay(1000);

    Apps::init();

    Serial.println("Running Lua app task...");
    Apps::startApp();
}

void loop()
{
   Apps::debugLoop();
}
