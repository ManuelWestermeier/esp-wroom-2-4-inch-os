// #include <Arduino.h>

// // #include "./screen.hpp"
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

//     // Serial.println("Running Screen...");
//     // screenInitTest();
// }

// void loop()
// {
//     // loopScreenTest();
// }
#include "./screen/config.h"

#include <TFT_eSPI.h> // Bodmer's TFT library
#include <SPI.h>

TFT_eSPI tft = TFT_eSPI(); // TFT instance

uint16_t touchY = 0, touchX = 0;

void setup()
{
    pinMode(27, OUTPUT);
    digitalWrite(27, HIGH);

    delay(5000);
    Serial.begin(115200);

    tft.init();
    tft.setRotation(2);
    tft.fillScreen(TFT_WHITE);

    tft.setTextColor(TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(20, 20);
    tft.println("Hello, World!!");

#ifdef TOUCH_CS
    tft.begin();
#endif
}

void drawBoxAt(uint16_t x, uint16_t y)
{
    const int boxSize = 3;
    tft.fillRect(x - boxSize / 2, y - boxSize / 2, boxSize, boxSize, TFT_BLUE);
}

void loop()
{
    if (tft.getTouch(&touchY, &touchX))
    {
        Serial.printf("Touch raw: %d,%d\n", touchY, touchX);

        drawBoxAt(touchX, 240 - touchY);
    }
}
