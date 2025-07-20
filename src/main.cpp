#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <TouchScreen.h>

#include "./lua.hpp"

// -------- Pin Definitionen --------

// TFT SPI Pins
#define TFT_CS 15
#define TFT_DC 2
#define TFT_RST 4 // Reset Pin (kann -1 sein, falls nicht angeschlossen)

#define TFT_MOSI 13
#define TFT_SCLK 14
#define TFT_MISO 12 // Meist nicht gebraucht

#define TFT_LED 21 // Hintergrundbeleuchtung (optional)

// Resistiver Touchscreen Pins (4-Draht)
#define YP 32 // Analog pin für Y+
#define XM 33 // Analog pin für X-
#define YM 25 // Digital pin für Y-
#define XP 26 // Digital pin für X+

// Kalibrierwerte (kann man anpassen, wenn nötig)
#define TS_MINX 150
#define TS_MAXX 3800
#define TS_MINY 130
#define TS_MAXY 4000

// Touch-Schwelle (wie stark gedrückt werden muss)
#define MINPRESSURE 10
#define MAXPRESSURE 1000

// Initialisierung TFT Display und Touchscreen
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);
TouchScreen ts = TouchScreen(XP, YP, XM, YM, 300);

void setup()
{
    Serial.begin(115200);
    Serial.println("Booting...");

    if (!SPIFFS.begin(true))
    {
        Serial.println("SPIFFS init failed");
        return;
    }

    run_lua_file("/test.lua");

    tft.begin();
    tft.setRotation(1); // Querformat
    tft.fillScreen(ILI9341_BLACK);
    pinMode(TFT_LED, OUTPUT);
    analogWrite(TFT_LED, 255); // Voll an
}

void loop()
{
    // Touchpunkt auslesen
    TSPoint p = ts.getPoint();

    // Pins wieder als OUTPUT setzen, damit Display funktioniert
    pinMode(XM, OUTPUT);
    pinMode(YP, OUTPUT);

    // Prüfen ob Touch erkannt und im Druckbereich ist
    if (p.z > MINPRESSURE && p.z < MAXPRESSURE)
    {
        // Koordinaten kalibrieren auf Displaygröße
        int x = map(p.x, TS_MINX, TS_MAXX, 0, tft.width());
        int y = map(p.y, TS_MINY, TS_MAXY, 0, tft.height());

        // Punkt zeichnen
        tft.fillCircle(x, y, 3, ILI9341_RED);

        Serial.printf("Touch: x=%d y=%d z=%d\n", x, y, p.z);
        delay(50); // kleine Pause, damit nicht zu viele Punkte entstehen
    }
}