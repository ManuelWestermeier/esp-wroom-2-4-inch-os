// #include <Adafruit_GFX.h>
// #include <Adafruit_ILI9341.h>
// #include <TouchScreen.h>

// // -------- Pin Definitionen --------

// // TFT SPI Pins
// #define TFT_CS 15
// #define TFT_DC 2
// #define TFT_RST 4 // Reset Pin (kann -1 sein, falls nicht angeschlossen)

// #define TFT_MOSI 13
// #define TFT_SCLK 14
// #define TFT_MISO 12 // Meist nicht gebraucht

// #define TFT_LED 21 // Hintergrundbeleuchtung (optional)

// // Resistiver Touchscreen Pins (4-Draht)
// #define YP 32 // Analog pin für Y+
// #define XM 33 // Analog pin für X-
// #define YM 25 // Digital pin für Y-
// #define XP 26 // Digital pin für X+

// // Kalibrierwerte (kann man anpassen, wenn nötig)
// #define TS_MINX 150
// #define TS_MAXX 3800
// #define TS_MINY 130
// #define TS_MAXY 4000

// // Touch-Schwelle (wie stark gedrückt werden muss)
// #define MINPRESSURE 10
// #define MAXPRESSURE 1000

// // Initialisierung TFT Display und Touchscreen
// Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);
// TouchScreen ts = TouchScreen(XP, YP, XM, YM, 300);

// void setup()
// {
//     Serial.begin(115200);
//     tft.begin();
//     tft.setRotation(1); // Querformat
//     tft.fillScreen(ILI9341_BLACK);
//     pinMode(TFT_LED, OUTPUT);
//     analogWrite(TFT_LED, 255); // Voll an
// }

// void loop()
// {
//     // Touchpunkt auslesen
//     TSPoint p = ts.getPoint();

//     // Pins wieder als OUTPUT setzen, damit Display funktioniert
//     pinMode(XM, OUTPUT);
//     pinMode(YP, OUTPUT);

//     // Prüfen ob Touch erkannt und im Druckbereich ist
//     if (p.z > MINPRESSURE && p.z < MAXPRESSURE)
//     {
//         // Koordinaten kalibrieren auf Displaygröße
//         int x = map(p.x, TS_MINX, TS_MAXX, 0, tft.width());
//         int y = map(p.y, TS_MINY, TS_MAXY, 0, tft.height());

//         // Punkt zeichnen
//         tft.fillCircle(x, y, 3, ILI9341_RED);

//         Serial.printf("Touch: x=%d y=%d z=%d\n", x, y, p.z);
//         delay(50); // kleine Pause, damit nicht zu viele Punkte entstehen
//     }
// }

#include <Arduino.h>
#include "../.pio/libdeps/esp32dev/mjs/mjs.h"

// Redirect printf to Serial
extern "C"
{
    int _write(int fd, const void *buf, size_t count)
    {
        for (size_t i = 0; i < count; i++)
        {
            Serial.write(((char *)buf)[i]);
        }
        return count;
    }
}

void foo(int x)
{
    printf("C Function: Hello from C! You passed int: %d\n", x);
    float f = 3.14;
    const char *s = "example";
    bool b = true;

    printf("Float: %.2f\n", f);
    printf("String: %s\n", s);
    printf("Bool: %s\n", b ? "true" : "false");
    printf("Hex: 0x%X\n", x);
    printf("Char: %c\n", (char)x);
}

void *my_dlsym(void *handle, const char *name)
{
    if (strcmp(name, "foo") == 0)
        return (void *)foo;
    return NULL;
}

void setup()
{
    Serial.begin(115200);
    while (!Serial)
        delay(10); // Wait for Serial to be ready

    printf("Setting up MJS...\n");

    struct mjs *mjs = mjs_create();
    mjs_set_ffi_resolver(mjs, my_dlsym);

    mjs_exec(mjs, "let f = ffi('void foo(int)'); f(65);", NULL);
}

void loop()
{
    // No need to loop anything
}
