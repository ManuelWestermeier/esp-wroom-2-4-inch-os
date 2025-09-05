#pragma once

#include "index.hpp"

extern "C"
{
#include "nanosvg.h" // nur einbinden, kein #define!
}

struct ESP32_SVG
{
    bool drawString(const String &svgString,
                    int xOff, int yOff,
                    int targetW, int targetH,
                    uint16_t color);
};
