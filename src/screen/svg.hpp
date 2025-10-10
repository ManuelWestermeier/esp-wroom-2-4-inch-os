#pragma once

#include "index.hpp"

extern "C"
{
#include "nanosvg.h" // Only include, do NOT #define NANOSVG_IMPLEMENTATION here
}

// ------------------------------------------------------
// Create or fetch an SVG image (with smart caching)
// ------------------------------------------------------
NSVGimage *createSVG(const String &svgString);

// ------------------------------------------------------
// Draw an SVG string directly on screen
// steps = number of steps per cubic Bezier (default = 4)
// ------------------------------------------------------
bool drawSVGString(const String &imageStr,
                   int xOff,
                   int yOff,
                   int targetW,
                   int targetH,
                   uint16_t color,
                   int steps = 4);
