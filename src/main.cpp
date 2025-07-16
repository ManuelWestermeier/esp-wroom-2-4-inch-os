#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <TouchScreen.h>

// TFT Pins (anpassen!)
#define TFT_CS 15
#define TFT_DC 2
#define TFT_RST 4

// Touch Pins (resistiv)
#define YP 34 // AnalogPin
#define XM 35 // AnalogPin
#define YM 32 // DigitalPin
#define XP 33 // DigitalPin

// TouchScreen Kalibrierung (muss ggf. angepasst werden)
#define TS_MINX 150
#define TS_MAXX 3800
#define TS_MINY 130
#define TS_MAXY 3900

Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);
TouchScreen ts = TouchScreen(XP, YP, XM, YM, 300);

void setup()
{
    Serial.begin(115200);
    tft.begin();
    tft.setRotation(1);
    tft.fillScreen(ILI9341_BLACK);
}

void loop()
{
    TSPoint p = ts.getPoint();

    // Pins wieder als Output, sonst funktioniert TFT nicht
    pinMode(XP, OUTPUT);
    pinMode(YP, OUTPUT);
    pinMode(XM, OUTPUT);
    pinMode(YM, OUTPUT);

    if (p.z > 10 && p.z < 1000)
    { // Touch erkannt
        // Map von Touch zu Display Koordinaten
        int x = map(p.x, TS_MINX, TS_MAXX, 0, tft.width());
        int y = map(p.y, TS_MINY, TS_MAXY, 0, tft.height());

        tft.fillCircle(x, y, 3, ILI9341_RED);
        delay(50);
    }
}
