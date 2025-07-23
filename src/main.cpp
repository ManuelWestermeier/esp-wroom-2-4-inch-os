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

#include <TFT_eSPI.h> // Bibliothek von Bodmer
#include <SPI.h>

TFT_eSPI tft = TFT_eSPI(); // TFT instance

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
}

void loop()
{
    // nichts
}
