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

TFT_eSPI tft = TFT_eSPI();   // TFT instance
TFT_eSPI_Button touchButton; // Optional: Für Buttons

uint16_t touchX = 0, touchY = 0; // Touch-Koordinaten
bool touched = false;

void setup()
{
    pinMode(27, OUTPUT);
    digitalWrite(27, HIGH);

    delay(5000);
    Serial.begin(115200);

    tft.init();
    tft.setRotation(1); // Querformat
    tft.fillScreen(TFT_BLACK);

    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(20, 100);
    tft.println("Hello, World!!");

// Touch kalibrieren, wenn nötig
#ifdef TOUCH_CS
    tft.begin();
#endif
}

// Funktion zum Zeichnen einer Box an der Touch-Stelle
void drawBoxAt(uint16_t x, uint16_t y)
{
    const int boxSize = 20;
    tft.drawRect(x - boxSize / 2, y - boxSize / 2, boxSize, boxSize, TFT_RED);
}

void loop()
{
    if (tft.getTouch(&touchX, &touchY))
    {
        Serial.printf("Touch at: %d, %d\n", touchX, touchY);
        drawBoxAt(touchX, touchY);
        delay(300); // Entprellung
    }
}