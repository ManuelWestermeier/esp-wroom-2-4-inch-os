#pragma once

#include <TFT_eSPI.h>
#include <FS.h>

extern "C"
{
#include "nanosvg.h" // nur einbinden, kein #define!
}

class ESP32_SVG
{
public:
    ESP32_SVG(TFT_eSPI *tft) : tft(tft) {}

    bool drawString(const String &svgString,
                    int xOff, int yOff,
                    int targetW, int targetH,
                    uint16_t color);

private:
    TFT_eSPI *tft;
};
