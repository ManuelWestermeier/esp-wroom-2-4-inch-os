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

uint16_t touchX = 0, touchY = 0;

void setup()
{
    pinMode(27, OUTPUT);
    digitalWrite(27, HIGH);

    delay(5000);
    Serial.begin(115200);

    tft.init();
    tft.setRotation(2);
    tft.fillScreen(TFT_BLACK);

    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(20, 100);
    tft.println("Hello, World!!");

#ifdef TOUCH_CS
    tft.begin();
#endif
}

void drawBoxAt(uint16_t x, uint16_t y)
{
    const int boxSize = 20;
    tft.drawRect(x - boxSize / 2, y - boxSize / 2, boxSize, boxSize, TFT_RED);
}

void loop()
{
    if (tft.getTouch(&touchX, &touchY))
    {
        // Touch-Mapping für setRotation(3)
        uint16_t correctedX = map(touchY, 0, 240, tft.width(), 0);  // invertiert
        uint16_t correctedY = map(touchX, 0, 320, tft.height(), 0); // invertiert

        Serial.printf("Touch raw: %d,%d -> mapped: %d,%d\n", touchX, touchY, correctedX, correctedY);

        drawBoxAt(correctedY, correctedX);
        delay(300);
    }
}
