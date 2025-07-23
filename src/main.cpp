// #include <Arduino.h>

// #include "./screen.hpp"
// #include "./apps/index.hpp"

// void setup()
// {
//     Serial.begin(115200);
//     Serial.println("Booting...");

//     // LuaApps::initialize(); // Initialisiere Serial + SPIFFS

//     // Serial.println("Running Lua app...");

//     // // Führt /test.lua im Sandbox-Modus aus
//     // int result = LuaApps::runApp("/test.lua", {"Arg1", "Hi"});
//     // Serial.printf("Lua App exited with code: %d\n", result);

//     Serial.println("Running Screen...");
//     screenInitTest();
// }

// void loop()
// {
//     loopScreenTest();
// }

#include <TFT_eSPI.h> // Hardware-specific library
#include <SPI.h>

TFT_eSPI tft = TFT_eSPI(); // Invoke library
TFT_eSPI_Button btn;       // Optional button for test

void setup()
{
    Serial.begin(115200);
    tft.init();
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);

    tft.setCursor(0, 0, 2);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);
    tft.println("ESP32 TFT Test");

    // Draw test graphics
    tft.fillCircle(120, 160, 50, TFT_RED);
    delay(500);
    tft.fillCircle(120, 160, 40, TFT_GREEN);
    delay(500);
    tft.fillCircle(120, 160, 30, TFT_BLUE);
}

void loop()
{
    // Add simple touchscreen detection if needed later
}
