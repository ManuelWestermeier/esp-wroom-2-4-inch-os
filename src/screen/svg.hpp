#pragma once

#include "index.hpp"

extern "C"
{
#include "nanosvg.h" // nur einbinden, kein #define!
}

bool drawSVGString(String svgString,
                   int xOff, int yOff,
                   int targetW, int targetH,
                   uint16_t color);
