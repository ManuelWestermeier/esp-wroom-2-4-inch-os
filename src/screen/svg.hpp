#pragma once

#include "index.hpp"

extern "C"
{
#include "nanosvg.h" // nur einbinden, kein #define!
}

NSVGimage *createSVG(const String &svgString);

bool drawSVGString(const String &imageStr,
                   int xOff, int yOff,
                   int targetW, int targetH,
                   uint16_t color, int steps = 4);

void updateSVGList();